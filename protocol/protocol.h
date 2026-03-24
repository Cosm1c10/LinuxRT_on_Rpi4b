#ifndef PROTOCOL_H
#define PROTOCOL_H

/*
 * protocol.h  —  Command protocol for Sentinel-RT
 *
 * Platform: Linux-RT (PREEMPT_RT) + OpenSSL
 */

#include <openssl/ssl.h>
#include "authorization.h"
#include "sensor_manager.h"

/* ------------------------------------------------------------------ */
/*  Connection context for one authenticated client session           */
/* ------------------------------------------------------------------ */
typedef struct {
    SSL            *ssl;          /* OpenSSL handle (mTLS session)    */
    ClientIdentity  identity;     /* Authenticated user (CN + role)   */
    SensorManager  *sensor_mgr;   /* Shared sensor manager instance   */
    int             running;      /* Loop-control flag                 */
} ProtocolContext;

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

/** protocol_init: Initialises a ProtocolContext for a new client. */
void protocol_init(ProtocolContext *ctx, SSL *ssl,
                   ClientIdentity id, SensorManager *mgr);

/** protocol_run: Main command loop. Blocks until client disconnects. */
void protocol_run(ProtocolContext *ctx);

/* Command handlers (called from protocol_run) */
void cmd_help(ProtocolContext *ctx);
void cmd_whoami(ProtocolContext *ctx);
void cmd_list_units(ProtocolContext *ctx);
void cmd_get_sensors(ProtocolContext *ctx);
void cmd_get_health(ProtocolContext *ctx);
void cmd_get_log(ProtocolContext *ctx);
void cmd_clear_log(ProtocolContext *ctx);
void cmd_monitor(ProtocolContext *ctx, const char *args);

/** send_response: Sends a string over the mTLS channel. */
void send_response(ProtocolContext *ctx, const char *msg);

/** send_eom: Sends the End-of-Message marker (0x03). */
void send_eom(ProtocolContext *ctx);

#endif /* PROTOCOL_H */
