#include <string.h>
#include <ctype.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include "authorization.h"

/* Uppercase a C string in-place */
static void to_upper_ascii(char *s) {
    if (!s) return;
    for (; *s; ++s)
        *s = (char)toupper((unsigned char)*s);
}

/* ------------------------------------------------------------------ */
/*  Role name lookup                                                   */
/* ------------------------------------------------------------------ */
const char *role_to_string(UserRole role) {
    switch (role) {
        case ROLE_ADMIN:       return "ADMIN";
        case ROLE_MAINTENANCE: return "MAINTENANCE";
        case ROLE_OPERATOR:    return "OPERATOR";
        case ROLE_VIEWER:      return "VIEWER";
        default:               return "UNAUTHORIZED";
    }
}

/* ------------------------------------------------------------------ */
/*  authorize_client  —  OpenSSL X.509 peer certificate parsing       */
/*                                                                     */
/*  Extracts CN (CommonName) and OU (OrganizationalUnit) from the     */
/*  client certificate, then maps OU → UserRole.                      */
/*                                                                     */
/*  Certificate OU values (set during cert generation):               */
/*    ADMIN | OPERATOR | MAINTENANCE | VIEWER                         */
/* ------------------------------------------------------------------ */
int authorize_client(SSL *ssl, ClientIdentity *out_id) {
    if (!ssl || !out_id) return -1;

    memset(out_id, 0, sizeof(*out_id));
    out_id->role = ROLE_UNAUTHORIZED;

    X509 *cert = SSL_get_peer_certificate(ssl);
    if (!cert) {
        fprintf(stderr, "[AUTH] No peer certificate presented\n");
        return -1;
    }

    X509_NAME *subj = X509_get_subject_name(cert);

    X509_NAME_get_text_by_NID(subj, NID_commonName,
                              out_id->common_name,
                              sizeof(out_id->common_name));

    char ou_buf[64] = {0};
    X509_NAME_get_text_by_NID(subj, NID_organizationalUnitName,
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
