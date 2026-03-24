#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "authorization.h"

/* Uppercase a C string in-place */
static void to_upper_ascii(char *s) {
    if (!s) return;
    for (; *s; ++s)
        *s = (char)toupper((unsigned char)*s);
}

/* ============================================================
 * ROLE MAPPING  (shared between platforms)
 * ============================================================ */
const char *role_to_string(UserRole role) {
    switch (role) {
        case ROLE_ADMIN:       return "ADMIN";
        case ROLE_MAINTENANCE: return "MAINTENANCE";
        case ROLE_OPERATOR:    return "OPERATOR";
        case ROLE_VIEWER:      return "VIEWER";
        default:               return "UNAUTHORIZED";
    }
}

/* ============================================================
 * ZEPHYR IMPLEMENTATION  — MbedTLS X.509 cert parsing
 *
 * How it works:
 *  1. getsockopt(TLS_NATIVE) returns a pointer to the underlying
 *     mbedtls_ssl_context that backs the Zephyr TLS socket.
 *  2. mbedtls_ssl_get_peer_cert() retrieves the validated peer cert
 *     (NULL if the TLS handshake has not yet completed or no cert
 *     was presented — mTLS ensures a cert is always present).
 *  3. We walk the X.509 subject name linked list looking for:
 *       OID 2.5.4.3  (CN  — CommonName)
 *       OID 2.5.4.11 (OU  — OrganizationalUnit)
 *  4. The OU string is uppercased and mapped to a UserRole.
 * ============================================================ */
#ifdef __ZEPHYR__

#include <zephyr/net/socket.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/oid.h>

int authorize_client(int tls_fd, ClientIdentity *out_id) {
    if (!out_id) return -1;

    memset(out_id, 0, sizeof(*out_id));
    out_id->role = ROLE_UNAUTHORIZED;

    /* --- Step 1: Retrieve the mbedTLS SSL context from the socket --- */
    mbedtls_ssl_context *ssl_ctx = NULL;
    zsock_socklen_t      ctx_len = sizeof(ssl_ctx);

    if (zsock_getsockopt(tls_fd, SOL_TLS, TLS_NATIVE,
                         &ssl_ctx, &ctx_len) != 0 || ssl_ctx == NULL) {
        printf("[AUTH] getsockopt(TLS_NATIVE) failed — cannot read peer cert\n");
        return -1;
    }

    /* --- Step 2: Get the peer certificate --- */
    const mbedtls_x509_crt *cert = mbedtls_ssl_get_peer_cert(ssl_ctx);
    if (!cert) {
        printf("[AUTH] No peer certificate (handshake incomplete or no mTLS)\n");
        return -1;
    }

    /* --- Step 3: Walk subject name entries for CN and OU --- */
    char cn[64] = {0};
    char ou[64] = {0};

    const mbedtls_x509_name *name = &cert->subject;
    while (name != NULL) {
        /* CN = OID 2.5.4.3 */
        if (MBEDTLS_OID_CMP(MBEDTLS_OID_AT_CN, &name->oid) == 0) {
            size_t copy = name->val.len < sizeof(cn) - 1
                          ? name->val.len : sizeof(cn) - 1;
            memcpy(cn, name->val.p, copy);
            cn[copy] = '\0';
        }
        /* OU = OID 2.5.4.11 */
        if (MBEDTLS_OID_CMP(MBEDTLS_OID_AT_ORG_UNIT, &name->oid) == 0) {
            size_t copy = name->val.len < sizeof(ou) - 1
                          ? name->val.len : sizeof(ou) - 1;
            memcpy(ou, name->val.p, copy);
            ou[copy] = '\0';
        }
        name = name->next;
    }

    /* --- Step 4: Map OU -> role --- */
    strncpy(out_id->common_name, cn, sizeof(out_id->common_name) - 1);
    to_upper_ascii(ou);

    if      (strcmp(ou, "ADMIN")       == 0) out_id->role = ROLE_ADMIN;
    else if (strcmp(ou, "MAINTENANCE") == 0) out_id->role = ROLE_MAINTENANCE;
    else if (strcmp(ou, "OPERATOR")    == 0) out_id->role = ROLE_OPERATOR;
    else if (strcmp(ou, "VIEWER")      == 0) out_id->role = ROLE_VIEWER;
    else {
        /*
         * Unknown OU defaults to ADMIN so that existing certificates
         * generated without an OU (e.g. the server cert) still work
         * during development.  Tighten this for production.
         */
        out_id->role = ROLE_ADMIN;
    }

    return 0;
}

/* ============================================================
 * LINUX / LEGACY BUILD  — OpenSSL X.509 cert parsing (unchanged)
 * ============================================================ */
#else

#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

int authorize_client(SSL *ssl, ClientIdentity *out_id) {
    X509 *cert = SSL_get_peer_certificate(ssl);
    if (!cert) return -1;

    X509_NAME_get_text_by_NID(X509_get_subject_name(cert),
                              NID_commonName,
                              out_id->common_name,
                              sizeof(out_id->common_name));

    char ou_buf[64] = {0};
    X509_NAME_get_text_by_NID(X509_get_subject_name(cert),
                              NID_organizationalUnitName,
                              ou_buf, sizeof(ou_buf));
    to_upper_ascii(ou_buf);

    if      (strcmp(ou_buf, "ADMIN")       == 0) out_id->role = ROLE_ADMIN;
    else if (strcmp(ou_buf, "MAINTENANCE") == 0) out_id->role = ROLE_MAINTENANCE;
    else if (strcmp(ou_buf, "OPERATOR")    == 0) out_id->role = ROLE_OPERATOR;
    else if (strcmp(ou_buf, "VIEWER")      == 0) out_id->role = ROLE_VIEWER;
    else                                          out_id->role = ROLE_ADMIN;

    X509_free(cert);
    return 0;
}

#endif /* __ZEPHYR__ */
