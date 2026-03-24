#ifndef AUTHORIZATION_H
#define AUTHORIZATION_H

/*
 * authorization.h
 *
 * Platform-agnostic header.  The function signature for authorize_client()
 * differs between Zephyr (int tls_fd) and Linux/legacy (SSL *ssl) builds,
 * selected at compile time via __ZEPHYR__.
 */

/* Only include OpenSSL on non-Zephyr builds */
#ifndef __ZEPHYR__
    #include <openssl/ssl.h>
#endif

typedef enum {
    ROLE_VIEWER = 0,
    ROLE_OPERATOR,
    ROLE_MAINTENANCE,
    ROLE_ADMIN,
    ROLE_UNAUTHORIZED
} UserRole;

typedef struct {
    char     common_name[64];
    UserRole role;
} ClientIdentity;

/**
 * authorize_client
 *
 * Zephyr: accepts a Zephyr TLS socket fd.  Extracts the peer certificate
 *         via getsockopt(TLS_NATIVE) -> mbedtls_ssl_get_peer_cert(), then
 *         parses CN and OU to assign a role.
 *
 * Linux:  accepts an OpenSSL SSL* handle (original behaviour).
 *
 * Returns 0 on success, -1 if the peer certificate is missing or invalid.
 */
#ifdef __ZEPHYR__
    int authorize_client(int tls_fd, ClientIdentity *out_id);
#else
    int authorize_client(SSL *ssl, ClientIdentity *out_id);
#endif

/**
 * role_to_string: Returns a human-readable role name.
 */
const char *role_to_string(UserRole role);

#endif /* AUTHORIZATION_H */
