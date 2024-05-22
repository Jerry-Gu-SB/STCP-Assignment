/*
 * transport.c 
 *
 * CS244a HW#3 (Reliable Transport)
 *
 * This file implements the STCP layer that sits between the
 * mysocket and network layers. You are required to fill in the STCP
 * functionality in this file. 
 *
 * For testing connection setup and teardown only, run server first in one terminal.
    $ ./server (or ./server -U for the unreliable network mode)
    Server's address is kyoungsoo-PC:42737
    Then, in another terminal on the same machine,
    $ ./client localhost:42737 (or ./client localhost:42737 -U for the unreliable network mode) client>
 */


#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "mysock.h"
#include "stcp_api.h"
#include "transport.h"


enum { CSTATE_ESTABLISHED };    /* obviously you should have more states */


/* this structure is global to a mysocket descriptor */
typedef struct
{
    bool_t done;    /* TRUE once connection is closed */

    int connection_state;   /* state of the connection (established, etc.) */
    tcp_seq initial_sequence_num;

    /* any other connection-wide global variables go here */
} context_t;


static void generate_initial_seq_num(context_t *ctx);
static void control_loop(mysocket_t sd, context_t *ctx);


void cleanup(context_t *ctx);

/* initialise the transport layer, and start the main loop, handling
 * any data from the peer or the application.  this function should not
 * return until the connection is closed.
 */


void server_send_syn(mysocket_t sd, context_t *ctx);
void server_receive_syn(mysocket_t sd, context_t *ctx);

void application_receive_data(mysocket_t sd, context_t *ctx);
void application_send_data(mysocket_t sd, context_t *ctx);

void transport_init(mysocket_t sd, bool_t is_active)
{
    context_t *ctx;

    ctx = (context_t *) calloc(1, sizeof(context_t));
    assert(ctx);

    generate_initial_seq_num(ctx);

    /* XXX: you should send a SYN packet here if is_active, or wait for one
     * to arrive if !is_active.
     *
     * TCP Handshake initialize
     */

    // 3 way TCP handshake
    if (is_active) {
        server_send_syn(sd, ctx);
    } else {
        server_receive_syn(sd, ctx);
    }

     /* after the handshake completes, unblock the
     * application with stcp_unblock_application(sd).  you may also use
     * this to communicate an error condition back to the application, e.g.
     * if connection fails; to do so, just set errno appropriately (e.g. to
     * ECONNREFUSED, etc.) before calling the function.
     */
    ctx->connection_state = CSTATE_ESTABLISHED;
    stcp_unblock_application(sd);

    control_loop(sd, ctx);


    // TODO: connection teardown
    
    /* do any cleanup here */
    cleanup(ctx);
}

void server_send_syn(mysocket_t sd, context_t *ctx) {
    // create a SYN packet

    if (stcp_network_send(sd, NULL, 0, 0) < 0) {
        errno = ECONNREFUSED;
        stcp_unblock_application(sd);
        cleanup(ctx);
        fprintf(stderr, "Server failed to send SYN packet\n");
        exit(-1);
    }
}

void server_receive_syn(mysocket_t sd, context_t *ctx) {
    struct timespec timeout;
    unsigned int event;
    clock_gettime(CLOCK_REALTIME, &timeout); // get current time
    timeout.tv_sec += 1; // add 1 second to the current time
    event = stcp_wait_for_event(sd, NETWORK_DATA, &timeout);
    if (event & NETWORK_DATA) {
        char buf[STCP_MSS];
        stcp_network_recv(sd, buf, STCP_MSS);
    } else {
        errno = ETIMEDOUT;
        stcp_unblock_application(sd);
        cleanup(ctx);
        fprintf(stderr, "Server failed to receive SYN packet\n");
        exit(-1);
    }
}

void cleanup(context_t *ctx) {
    free(ctx);
}


/* generate initial sequence number for an STCP connection */
static void generate_initial_seq_num(context_t *ctx)
{
    assert(ctx);
    ctx->initial_sequence_num = 1;
}


/* control_loop() is the main STCP loop; it repeatedly waits for one of the
 * following to happen:
 *   - incoming data from the peer
 *   - new data from the application (via mywrite())
 *   - the socket to be closed (via myclose())
 *   - a timeout
 */
static void control_loop(mysocket_t sd, context_t *ctx)
{
    assert(ctx);

    while (!ctx->done)
    {
        unsigned int event;

        /* see stcp_api.h or stcp_api.c for details of this function */
        /* TODO: you will need to change some of these arguments! */
        event = stcp_wait_for_event(sd, 0, NULL);

        /* check whether it was the network, app, or a close request */
        if (event & APP_DATA)
        {
            /* the application has requested that data be sent */
            /* see stcp_app_recv() */
        }

        /* etc. */
    }
}


/**********************************************************************/
/* our_dprintf
 *
 * Send a formatted message to stdout.
 * 
 * format               A printf-style format string.
 *
 * This function is equivalent to a printf, but may be
 * changed to log errors to a file if desired.
 *
 * Calls to this function are generated by the dprintf amd
 * dperror macros in transport.h
 */
void our_dprintf(const char *format,...)
{
    va_list argptr;
    char buffer[1024];

    assert(format);
    va_start(argptr, format);
    vsnprintf(buffer, sizeof(buffer), format, argptr);
    va_end(argptr);
    fputs(buffer, stdout);
    fflush(stdout);
}



