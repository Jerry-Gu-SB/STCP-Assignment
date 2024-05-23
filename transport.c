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

int DEFAULT_OFFSET = 5;
int DEFAULT_WINDOW_SIZE = 3072;
enum {
    CSTATE_ESTABLISHED,
    CSTATE_SYN_SENT,
    CSTATE_SYN_RECEIVED,
    CSTATE_CLOSED
};


/* this structure is global to a mysocket descriptor */
typedef struct
{
    bool_t done;    /* TRUE once connection is closed */
    int connection_state;   /* state of the connection (established, etc.) */
    tcp_seq initial_sequence_num;

    /* any other connection-wide global variables go here */

    /* Sliding window variables */
    tcp_seq send_base;      /* Base of the send window */
    tcp_seq next_seq_num;   /* Next sequence number to be sent */
    tcp_seq recv_base;      /* Base of the receive window */
    tcp_seq expected_seq_num; /* Expected sequence number to be received */
} context_t;


static void generate_initial_seq_num(context_t *ctx);
static void control_loop(mysocket_t sd, context_t *ctx);


/* initialise the transport layer, and start the main loop, handling
 * any data from the peer or the application.  this function should not
 * return until the connection is closed.
 */


void transport_init(mysocket_t sd, bool_t is_active)
{

    context_t *ctx;

    ctx = (context_t *) calloc(1, sizeof(context_t));
    assert(ctx);

    generate_initial_seq_num(ctx);

    // initialize the connection state
    ctx->send_base = ctx->initial_sequence_num;
    ctx->next_seq_num = ctx->initial_sequence_num;
    ctx->recv_base = 0;
    ctx->expected_seq_num = 0;
    /* XXX: you should send a SYN packet here if is_active, or wait for one
     * to arrive if !is_active.
     *
     * TCP Handshake initialize
     */

    // 3 way TCP handshake
    if (is_active) {
        // Active open
        // Send SYN
//        struct tcphdr *syn_packet = (struct tcphdr *) calloc(1, sizeof(struct tcphdr));
//        syn_packet->th_flags = TH_SYN;  // syn packet
//        syn_packet->th_seq = ctx->initial_sequence_num;
//        syn_packet->th_off = DEFAULT_OFFSET;
//        syn_packet->th_win = DEFAULT_WINDOW_SIZE;
//        stcp_network_send(sd, &syn_packet, sizeof(*syn_packet), NULL);
        struct tcphdr syn_packet;
        memset(&syn_packet, 0, sizeof(syn_packet));
        syn_packet.th_flags = TH_SYN;
        syn_packet.th_seq = ctx->initial_sequence_num;
        syn_packet.th_off = DEFAULT_OFFSET;
        syn_packet.th_win = DEFAULT_WINDOW_SIZE;
        stcp_network_send(sd, &syn_packet, sizeof(syn_packet), NULL);

        ctx->connection_state = CSTATE_SYN_SENT;


        struct tcphdr syn_ack_packet;
        stcp_wait_for_event(sd, NETWORK_DATA, NULL);
        stcp_network_recv(sd, &syn_ack_packet, sizeof(syn_ack_packet));

        if (syn_ack_packet.th_flags == (TH_SYN | TH_ACK)) {
            struct tcphdr ack_packet;
            memset(&ack_packet, 0, sizeof(ack_packet));
            ack_packet.th_flags = TH_ACK;
            ack_packet.th_seq = ctx->initial_sequence_num + 1;
            ack_packet.th_ack = syn_ack_packet.th_seq + 1;
            ack_packet.th_off = DEFAULT_OFFSET;  // Header length (5 * 32-bit words = 20 bytes)
            ack_packet.th_win = DEFAULT_WINDOW_SIZE;  // Window size
            stcp_network_send(sd, &ack_packet, sizeof(ack_packet), NULL);

            ctx->recv_base = syn_ack_packet.th_seq + 1;
            ctx->expected_seq_num = ctx->recv_base;

            ctx->connection_state = CSTATE_ESTABLISHED;
        } else {
            errno = ECONNREFUSED;
            free(ctx);
            return;
        }
    } else {
        // Passive open
        ctx->connection_state = CSTATE_CLOSED;

        struct tcphdr syn_packet;
        stcp_wait_for_event(sd, NETWORK_DATA, NULL);
        stcp_network_recv(sd, &syn_packet, sizeof(syn_packet));

        if (syn_packet.th_flags == TH_SYN) {
            struct tcphdr syn_ack_packet;
            memset(&syn_ack_packet, 0, sizeof(syn_ack_packet));
            syn_ack_packet.th_flags = TH_SYN | TH_ACK;
            syn_ack_packet.th_seq = ctx->initial_sequence_num;
            syn_ack_packet.th_ack = syn_packet.th_seq + 1;
            syn_ack_packet.th_off = DEFAULT_OFFSET;
            syn_ack_packet.th_win = DEFAULT_WINDOW_SIZE;
            stcp_network_send(sd, &syn_ack_packet, sizeof(syn_ack_packet), NULL);

            ctx->connection_state = CSTATE_SYN_RECEIVED;
        }
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



