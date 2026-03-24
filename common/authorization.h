#ifndef AUTHORIZATION_H
#define AUTHORIZATION_H

/*
 * authorization.h  —  Role-based client authentication via mTLS
 *
 * Platform: Linux-RT (PREEMPT_RT) + OpenSSL
 *
 * authorize_client() extracts the CN and OU fields from the client's
 * X.509 certificate (presented during mTLS handshake) and maps the
 * OU to a UserRole.
 */

#include <openssl/ssl.h>

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
 * Extracts CN and OU from the peer X.509 certificate on @ssl and
 * maps the OU string to a UserRole written into @out_id.
 *
 * Returns 0 on success, -1 if the peer certificate is missing or invalid.
 */
int authorize_client(SSL *ssl, ClientIdentity *out_id);

/**
 * role_to_string: Returns a human-readable role name.
 */
const char *role_to_string(UserRole role);

#endif /* AUTHORIZATION_H */
