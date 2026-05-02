/*
 * sin_client.c - worker client for the 's' (sin) operator.
 *
 * Flow:
 *  1. Create own channel + timer (100 ms tick).
 *  2. Register with server via MsgSend('r', oper='s', chid=own chid).
 *     Unlike arithmetic workers, we do NOT stay blocked on that send —
 *     we want the reply immediately (EOK) so we can return to our pulse loop.
 *     The server stores our coid (opened via ConnectAttach to our chid).
 *  3. Loop on MsgReceivePulse(chid):
 *       TIMER pulse  -> increment counter, recompute sin (scaled x1000)
 *       MY_PULSE_CODE (from server) -> send MsgSend('a') to server with result
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/dispatch.h>
#include <math.h>
#include "common.h"

#define TIMER_PULSE_CODE (_PULSE_CODE_MINAVAIL + 1)  /* must differ from MY_PULSE_CODE */

int server_coid = -1;

int main(void) {
    struct sigevent   event;
    struct itimerspec itime;
    timer_t           timer_id;
    int               chid, coid;
    struct sched_param sp;
    int               prio;
    int               timer_count = 0;
    int               sinT        = 0;     /* sin(timer_count)*1000, integer scaled */

    /* ── 1. Create our own channel ── */
    chid = ChannelCreate(0);
    if (chid == -1) { perror("ChannelCreate"); return EXIT_FAILURE; }

    /* ── 2. Self-connection so we can send pulses to ourselves ── */
    coid = ConnectAttach(0, 0, chid, _NTO_SIDE_CHANNEL, 0);
    if (coid == -1) { perror("ConnectAttach"); return EXIT_FAILURE; }

    /* ── 3. Timer: 100 ms repeating, fires TIMER_PULSE_CODE pulse ── */
    if (SchedGet(0, 0, &sp) != -1)
        prio = sp.sched_priority;
    else
        prio = 10;

    SIGEV_PULSE_INIT(&event, coid, prio, TIMER_PULSE_CODE, 0);
    if (timer_create(CLOCK_MONOTONIC, &event, &timer_id) == -1) {
        perror("timer_create"); return EXIT_FAILURE;
    }
    itime.it_value.tv_sec     = 0;
    itime.it_value.tv_nsec    = 100000000;   /* 100 ms first fire */
    itime.it_interval.tv_sec  = 0;
    itime.it_interval.tv_nsec = 100000000;   /* 100 ms period     */
    timer_settime(timer_id, 0, &itime, NULL);

    /* ── 4. Connect to server ── */
    server_coid = name_open(ATTACH_POINT, 0);
    if (server_coid == -1) { perror("name_open"); return EXIT_FAILURE; }
    printf("sin_client: connected to server (coid=%d)\n", server_coid);

    /* ── 5. Register with server ──
     *
     * We pass our chid so the server can call ConnectAttach()+MsgSendPulse()
     * back to us later.  We expect an immediate EOK reply (unlike arithmetic
     * workers that stay blocked here).
     */
    my_msg_t send_msg, recv_msg;
    memset(&send_msg, 0, sizeof(send_msg));
    send_msg.type = 'r';
    send_msg.oper = 's';
    send_msg.chid = chid;          /* tell server where to pulse us */

    if (MsgSend(server_coid, &send_msg, sizeof(send_msg),
                &recv_msg, sizeof(recv_msg)) == -1) {
        perror("MsgSend register"); return EXIT_FAILURE;
    }
    printf("sin_client: registered with server, entering pulse loop\n");

    /* ── 6. Main pulse loop ── */
    while (1) {
        struct _pulse pulse;
        /*
         * MsgReceivePulse blocks on OUR channel (chid), not on server_coid.
         * Two pulse codes can arrive:
         *   TIMER_PULSE_CODE : 100 ms tick → update counter & sin value
         *   MY_PULSE_CODE    : server asking for current sin value
         */
        int rc = MsgReceivePulse(chid, &pulse, sizeof(pulse), NULL);
        if (rc == -1) { perror("MsgReceivePulse"); break; }

        switch (pulse.code) {
			case TIMER_PULSE_CODE:
				timer_count++;
				/* Scale to integer: store sin()*1000 so we keep 3 decimal digits */
				sinT = (int)(sin((double)timer_count) * 1000.0);
				/* Uncomment for debugging the counter:
				* printf("sin_client: tick %d  sin=%d (x1000)\n", timer_count, sinT);
				*/
				break;

			case MY_PULSE_CODE:
				/*
				* Server pulsed us: it has an euc waiting for sin(counter).
				* Reply by sending an 'a' (answer) message to the server.
				* This MsgSend also blocks until the server replies, but that
				* reply arrives almost immediately (server does MsgReply to us
				* as an ack, then forwards sinT to the euc).
				*/
				printf("sin_client: pulse from server, sending sin(%d)=%d (x1000)\n",
					timer_count, sinT);

				memset(&send_msg, 0, sizeof(send_msg));
				send_msg.type   = 'a';
				send_msg.oper   = 's';
				send_msg.result = sinT;
				/* rcvid_euc will be filled in by the server when it stored
				* the pending euc — we do not know it here, leave 0 */

				if (MsgSend(server_coid, &send_msg, sizeof(send_msg),
							&recv_msg, sizeof(recv_msg)) == -1) {
					perror("MsgSend answer"); break;
				}
				/* Server acked our 'a' → we're free to keep ticking */
				break;

			default:
				printf("sin_client: unknown pulse code %d\n", pulse.code);
				break;
        }
    }

    timer_delete(timer_id);
    name_close(server_coid);
    printf("sin_client: exit.\n");
    return EXIT_SUCCESS;
}