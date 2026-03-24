#ifndef PROTOCOL_H
#define PROTOCOL_H

/*
 * protocol.h
 *
 * The ProtocolContext struct holds the TLS connection handle.
 * On Zephyr this is an int (TLS socket fd from zsock_accept).
 * On Linux/legacy builds it remains an OpenSSL SSL*.
 */

#ifdef __ZEPHYR__
    #include <zephyr/net/socket.h>
#else
    #include <openssl/ssl.h>
#endif

#include "authorization.h"
#include "sensor_manager.h"

/* ============================================================
 * DATA STRUCTURES
 * ============================================================ */

typedef struct {
#ifdef __ZEPHYR__
    int tls_fd;             /* Zephyr TLS socket fd (zsock_accept result) */
#else
    SSL *ssl;               /* OpenSSL secure socket handle                */
#endif
    ClientIdentity  identity;    /* Authenticated user (CN + role)         */
    SensorManager  *sensor_mgr;  /* Shared hardware manager                */
    int             running;     /* Loop-control flag                       */
} ProtocolContext;

/* ============================================================
 * PUBLIC API
 * ============================================================ */

/**
 * protocol_init: Prepares a ProtocolContext for a new client session.
 *
 * Zephyr:  pass the TLS socket fd returned by zsock_accept().
 * Linux:   pass the SSL* returned by SSL_new() + SSL_accept().
 */
#ifdef __ZEPHYR__
void protocol_init(ProtocolContext *ctx, int tls_fd,
                   ClientIdentity id, SensorManager *mgr);
#else
void protocol_init(ProtocolContext *ctx, SSL *ssl,
                   ClientIdentity id, SensorManager *mgr);
#endif

/**
 * protocol_run: Main command-processing loop for one client session.
 * Blocks until the client disconnects or sends "quit".
 */
void protocol_run(ProtocolContext *ctx);

/* ============================================================
 * COMMAND HANDLERS (called from protocol_run)
 * ============================================================ */
void cmd_help(ProtocolContext *ctx);
void cmd_whoami(ProtocolContext *ctx);
void cmd_list_units(ProtocolContext *ctx);
void cmd_get_sensors(ProtocolContext *ctx);
void cmd_get_health(ProtocolContext *ctx);
void cmd_get_log(ProtocolContext *ctx);
void cmd_clear_log(ProtocolContext *ctx);
void cmd_monitor(ProtocolContext *ctx, const char *args);

/* ============================================================
 * PROTOCOL HELPERS
 * ============================================================ */

/** send_response: Sends a string over the encrypted TLS channel. */
void send_response(ProtocolContext *ctx, const char *msg);

/** send_eom: Sends the End-of-Message marker (0x03) to reset the client prompt. */
void send_eom(ProtocolContext *ctx);

#endif /* PROTOCOL_H */
