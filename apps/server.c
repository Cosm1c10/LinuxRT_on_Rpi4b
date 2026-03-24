/*
 * server.c  —  Sentinel-RT mTLS Server  (Linux-RT / PREEMPT_RT)
 *
 * Build:
 *   make server
 *
 * Runtime requirements:
 *   - PREEMPT_RT kernel (uname -a should show "PREEMPT_RT")
 *   - Run as root (or with CAP_SYS_NICE) for SCHED_FIFO
 *   - Certs in certs/ (generate with: ./scripts/quick_start.sh)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "authorization.h"
#include "sensor_manager.h"
#include "protocol.h"

#define PORT                    8080
#define MAX_CONCURRENT_SESSIONS 32

/* RT worker thread priority (SCHED_FIFO, 1=lowest…99=highest) */
#define RT_WORKER_PRIORITY      50

#define SERVER_CERT "certs/server.crt"
#define SERVER_KEY  "certs/server.key"
#define CA_CERT     "certs/ca.crt"

/* ------------------------------------------------------------------ */
/*  Session accounting                                                 */
/* ------------------------------------------------------------------ */
static pthread_mutex_t session_mutex        = PTHREAD_MUTEX_INITIALIZER;
static int             current_sessions     = 0;
static int             max_observed_sessions = 0;

static int try_reserve_session_slot(void) {
    int ok = 0;
    pthread_mutex_lock(&session_mutex);
    if (current_sessions < MAX_CONCURRENT_SESSIONS) {
        current_sessions++;
        if (current_sessions > max_observed_sessions)
            max_observed_sessions = current_sessions;
        ok = 1;
    }
    printf("[SESSIONS] Current: %d | Max: %d | Limit: %d\n",
           current_sessions, max_observed_sessions, MAX_CONCURRENT_SESSIONS);
    pthread_mutex_unlock(&session_mutex);
    return ok;
}

static void release_session_slot(void) {
    pthread_mutex_lock(&session_mutex);
    if (current_sessions > 0) current_sessions--;
    printf("[SESSIONS] Current: %d | Max: %d | Limit: %d\n",
           current_sessions, max_observed_sessions, MAX_CONCURRENT_SESSIONS);
    pthread_mutex_unlock(&session_mutex);
}

/* ------------------------------------------------------------------ */
/*  Per-client session                                                 */
/* ------------------------------------------------------------------ */
typedef struct {
    int                client_fd;
    SSL_CTX           *ssl_ctx;
    SensorManager     *sensor_mgr;
    struct sockaddr_in client_addr;
} ClientSession;

/* ------------------------------------------------------------------ */
/*  Worker thread: one per connected client                           */
/* ------------------------------------------------------------------ */
static void *client_session_thread(void *arg) {
    ClientSession *session = (ClientSession *)arg;
    ClientIdentity id;
    char ip_buf[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &session->client_addr.sin_addr, ip_buf, sizeof(ip_buf));

    SSL *ssl = SSL_new(session->ssl_ctx);
    if (!ssl) {
        fprintf(stderr, "[ERROR] SSL_new failed for %s\n", ip_buf);
        close(session->client_fd);
        free(session);
        release_session_slot();
        return NULL;
    }

    SSL_set_fd(ssl, session->client_fd);

    if (SSL_accept(ssl) <= 0) {
        ERR_print_errors_fp(stderr);
        fprintf(stderr, "[AUTH] TLS handshake failed from %s\n", ip_buf);
    } else if (authorize_client(ssl, &id) == 0) {
        if (id.role != ROLE_UNAUTHORIZED) {
            printf("[AUTH] GRANTED — User: '%s' | Role: '%s' | IP: %s\n",
                   id.common_name, role_to_string(id.role), ip_buf);

            ProtocolContext pctx;
            protocol_init(&pctx, ssl, id, session->sensor_mgr);
            protocol_run(&pctx);

            printf("[CONN] Session ended for '%s'\n", id.common_name);
        } else {
            printf("[AUTH] DENIED — unauthorized role for '%s' from %s\n",
                   id.common_name, ip_buf);
        }
    } else {
        printf("[AUTH] DENIED — missing/invalid certificate from %s\n", ip_buf);
    }

    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(session->client_fd);
    free(session);
    release_session_slot();
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */
int main(void) {
    /* Lock all current and future memory pages — prevents RT latency
     * spikes from page faults during sensor / TLS operations.         */
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0)
        fprintf(stderr, "[WARN] mlockall failed (%s) — run as root for RT guarantees\n",
                strerror(errno));

    signal(SIGPIPE, SIG_IGN);

    printf("====================================================\n");
    printf("  Sentinel-RT Monitoring System (Linux-RT / RPi 4) \n");
    printf("====================================================\n");

    /* Verify PREEMPT_RT kernel */
    FILE *f = fopen("/sys/kernel/realtime", "r");
    if (f) {
        char c = '0';
        if (fread(&c, 1, 1, f) == 1 && c == '1')
            printf("[RT] PREEMPT_RT kernel confirmed.\n");
        else
            printf("[WARN] Kernel does not appear to be PREEMPT_RT patched.\n");
        fclose(f);
    } else {
        printf("[WARN] /sys/kernel/realtime not found — cannot verify RT kernel.\n");
    }

    /* Sensor manager */
    SensorManager sensor_mgr;
    if (manager_init(&sensor_mgr) != 0) {
        fprintf(stderr, "[FATAL] Sensor manager init failed\n");
        return 1;
    }
    printf("[SYSTEM] Polling active (Vib:GPIO%d Snd:GPIO%d Temp:GPIO%d Cur:I2C)\n",
           PIN_VIBRATION, PIN_SOUND, PIN_TEMP_1W);

    /* TLS setup */
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();

    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx) { ERR_print_errors_fp(stderr); return 1; }

    if (SSL_CTX_use_certificate_file(ctx, SERVER_CERT, SSL_FILETYPE_PEM) <= 0 ||
        SSL_CTX_use_PrivateKey_file(ctx, SERVER_KEY,   SSL_FILETYPE_PEM) <= 0 ||
        SSL_CTX_load_verify_locations(ctx, CA_CERT, NULL) <= 0) {
        ERR_print_errors_fp(stderr);
        fprintf(stderr, "[FATAL] TLS cert/key load failed. "
                        "Run ./scripts/quick_start.sh first.\n");
        return 1;
    }
    SSL_CTX_set_verify(ctx,
                       SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                       NULL);

    /* TCP socket */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    int opt  = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };
    bind(sock,   (struct sockaddr *)&addr, sizeof(addr));
    listen(sock, 5);

    printf("[NETWORK] Listening on port %d (mTLS)\n\n", PORT);

    /* pthread attributes for RT worker threads */
    pthread_attr_t rt_attr;
    struct sched_param rt_param = { .sched_priority = RT_WORKER_PRIORITY };
    pthread_attr_init(&rt_attr);
    pthread_attr_setinheritsched(&rt_attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&rt_attr, SCHED_FIFO);
    pthread_attr_setschedparam(&rt_attr, &rt_param);
    pthread_attr_setdetachstate(&rt_attr, PTHREAD_CREATE_DETACHED);

    /* Accept loop */
    while (1) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);

        printf("[NETWORK] Waiting for connection...\n");
        int cfd = accept(sock, (struct sockaddr *)&caddr, &clen);
        if (cfd < 0) { perror("accept"); continue; }

        char ip_buf[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &caddr.sin_addr, ip_buf, sizeof(ip_buf));
        printf("[CONN] TCP connection from %s\n", ip_buf);

        if (!try_reserve_session_slot()) {
            printf("[SESSIONS] Limit reached, rejecting %s\n", ip_buf);
            close(cfd);
            continue;
        }

        ClientSession *s = malloc(sizeof(ClientSession));
        if (!s) {
            fprintf(stderr, "[ERROR] OOM allocating session\n");
            close(cfd);
            release_session_slot();
            continue;
        }
        s->client_fd   = cfd;
        s->ssl_ctx     = ctx;
        s->sensor_mgr  = &sensor_mgr;
        s->client_addr = caddr;

        pthread_t tid;
        if (pthread_create(&tid, &rt_attr, client_session_thread, s) != 0) {
            fprintf(stderr, "[WARN] pthread_create RT failed (%s), "
                            "retrying with default scheduler\n", strerror(errno));
            /* Fallback: create without RT attributes */
            if (pthread_create(&tid, NULL, client_session_thread, s) != 0) {
                perror("[ERROR] pthread_create");
                close(cfd); free(s); release_session_slot();
                continue;
            }
            pthread_detach(tid);
        }
    }

    pthread_attr_destroy(&rt_attr);
    close(sock);
    SSL_CTX_free(ctx);
    manager_cleanup(&sensor_mgr);
    return 0;
}
