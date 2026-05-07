/*
 * ex3_enduser_client.c - interactive user of the calculator.
 *
 * Role:
 *   - Open a connection to the server by name.
 *   - Prompt the user for "number operator number".
 *   - Pack the input into a message of type 'o' (operation request) and
 *     MsgSend it to the server. We then stay blocked until a reply
 *     comes back.
 *   - The reply is either:
 *       type 'a' -> contains the result (normal case)
 *       type 'e' -> no worker for this operator, or div-by-zero
 *   - Type 'q' as operator to quit.
 *
 * Note: this client does NOT talk to the workers directly. It only
 * speaks to the server, which is in charge of forwarding the job and
 * routing the answer back. This client can always reach the server
 * even if no worker is running - it will just get an 'e' reply.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/dispatch.h>
#include "common.h"

int server_coid = -1;

int main(void) {
    char  operation, c;
    int   num1, num2, argsNum;
    my_msg_t send_msg, reply_msg;

    /* Find the server by its published name. Fails if server not running. */
    server_coid = name_open(ATTACH_POINT, 0);
    if (server_coid == -1) {
        perror("name_open � is the server running?");
        return EXIT_FAILURE;
    }
    printf("Connected to calc_svr (coid=%d)\n", server_coid);

    while (1) {
        printf("Enter [number] [operator] [number]: ");
        fflush(stdout);
        argsNum = scanf("%d %c %d", &num1, &operation, &num2);

        /* Local input validation - we do not bother the server with
         * malformed commands. On bad input we flush the rest of the
         * line so the next scanf starts clean. */
        if (argsNum != 3) {
            printf("Bad input, try again.\n");
            while ((c = getchar()) != '\n' && c != EOF);
            continue;
        }
        if (operation == 'q')
            break;
//        if (strchr("+-*/s", operation) == NULL) {
//            printf("Operation '%c' not supported. Use + - * / s\n", operation);
//            continue;
//        }

        /* Build the request ('o' = operation). MsgSend blocks until the
         * server routes the job to a worker, gets the answer back, and
         * replies to us. */
        memset(&send_msg, 0, sizeof(send_msg));
        send_msg.type   = 'o';
        send_msg.oper   = operation;
        send_msg.arg1   = num1;
        send_msg.arg2   = num2;

        printf("Sending: %d %c %d\n", num1, operation, num2);
        int status = MsgSend(server_coid, &send_msg, sizeof(send_msg), &reply_msg, sizeof(reply_msg));
        if (status == -1) {
            perror("MsgSend");
            break;
        }

		/* 'e' reply = worker missing or refused the op (e.g. div by 0).
		 * Otherwise the reply carries the result the worker computed. */
		if (reply_msg.type == 'e') {
			printf("Error worker not active or operation not permitted\n");
		} else {
			printf("Result: %d %c %d = %d\n", reply_msg.arg1, reply_msg.oper, reply_msg.arg2, reply_msg.result);
		}
    }

    name_close(server_coid);
    printf("End-user client exit.\n");
    return EXIT_SUCCESS;
}
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
    char 			  worker_type = 's';

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
    send_msg.oper = worker_type;
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
				send_msg.oper   = worker_type;
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
