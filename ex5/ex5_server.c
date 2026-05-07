/*
 * ex5_server.c — generic server supporting both reply-driven and
 * pulse-driven workers for ANY operator.
 *
 * How the server decides:
 *   - Registration msg with chid != 0  →  pulse-driven worker
 *   - Registration msg with chid == 0  →  reply-driven worker
 *
 * Only one worker per operator at a time, regardless of comm style.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/dispatch.h>
#include "common.h"

typedef struct registration {
    char oper;
    int  rcvid_wc;       /* reply-driven: >0 idle, -1 busy, 0 free      */
    int  pulse_coid;     /* pulse-driven: coid to pulse worker (0 = n/a) */
    int  pending_euc;    /* pulse-driven: rcvid of euc waiting (-1 none) */
} registration_t;

#define MAX_WORKERS 1024
registration_t regs[MAX_WORKERS];
int nregs = 0;

static int find_worker(char oper) {
    int i;
    for (i = 0; i < nregs; i++)
        if (regs[i].oper == oper &&
            (regs[i].rcvid_wc > 0 || regs[i].pulse_coid != 0))
            return i;
    return -1;
}

static int find_reg(char oper) {
    int i;
    for (i = 0; i < nregs; i++)
        if (regs[i].oper == oper &&
            (regs[i].rcvid_wc != 0 || regs[i].pulse_coid != 0))
            return i;
    return -1;
}

void rcvid_to_null(int rcvid) {
    int i;
    for (i = 0; i < nregs; i++)
        if (regs[i].rcvid_wc == rcvid && regs[i].rcvid_wc != -1)
            regs[i].rcvid_wc = 0;
}

int main(void) {
    name_attach_t *attach;
    my_msg_t       msg;
    int            rcvid;
    struct _msg_info info;

    printf("Server starting...\n");

    if ((attach = name_attach(NULL, ATTACH_POINT, 0)) == NULL) {
        perror("name_attach"); return EXIT_FAILURE;
    }
    printf("name_attach OK, registered as '%s'\n", ATTACH_POINT);

    while (1) {
    	rcvid = MsgReceive(attach->chid, &msg, sizeof(msg), &info);
        if (rcvid == -1) { perror("MsgReceive"); break; }

        /* ── Pulse handling (unchanged) ── */
        if (rcvid == 0) {
            int id = 0;
            switch (msg.hdr.code) {
            case _PULSE_CODE_DISCONNECT:
                ConnectDetach(msg.hdr.scoid);
                id = msg.hdr.value.sival_int;
                rcvid_to_null(id);
                break;
            case _PULSE_CODE_UNBLOCK:
                ConnectDetach(msg.hdr.scoid);
                id = msg.hdr.value.sival_int;
                rcvid_to_null(id);
                break;
            default:
                break;
            }
            continue;
        }

        if (msg.hdr.type == _IO_CONNECT) { MsgReply(rcvid, EOK, NULL, 0); continue; }
        if (msg.hdr.type > _IO_BASE && msg.hdr.type <= _IO_MAX) { MsgError(rcvid, ENOSYS); continue; }

        /* ── 'r' : worker registration ── */
        if (msg.type == 'r') {
            printf("Server: worker '%c' registering (rcvid=%d, chid=%d)\n",
                   msg.oper, rcvid, msg.chid);

            /* Universal duplicate check — one worker per oper, any comm style */
            int i, dup = 0, reuse = -1;
            for (i = 0; i < nregs; i++) {
                if (regs[i].oper != msg.oper) continue;
                if (regs[i].rcvid_wc > 0 || regs[i].rcvid_wc == -1
                    || regs[i].pulse_coid != 0) {
                    dup = 1; break;
                }
                if (regs[i].rcvid_wc == 0 && regs[i].pulse_coid == 0)
                    reuse = i;
            }
            if (dup) {
                MsgError(rcvid, EBADSLT);

            } else if (msg.chid != 0) {
                /* ── Pulse-driven registration ── */
                int sc = ConnectAttach(0, info.pid, msg.chid, _NTO_SIDE_CHANNEL, 0);
                if (sc == -1) {
                    perror("ConnectAttach to pulse client");
                    MsgError(rcvid, errno);
                } else {
                    int slot = (reuse != -1) ? reuse : nregs++;
                    regs[slot].oper       = msg.oper;
                    regs[slot].rcvid_wc   = 0;
                    regs[slot].pulse_coid = sc;
                    regs[slot].pending_euc = -1;
                    MsgReply(rcvid, EOK, NULL, 0);
                    printf("Server: pulse-driven '%c' registered, coid=%d\n",
                           msg.oper, sc);
                }

            } else {
                /* ── Reply-driven registration ── */
                int slot = (reuse != -1) ? reuse : nregs++;
                regs[slot].oper       = msg.oper;
                regs[slot].rcvid_wc   = rcvid;
                regs[slot].pulse_coid = 0;
                regs[slot].pending_euc = -1;
                printf("Server: reply-driven '%c' registered, rcvid=%d\n",
                       msg.oper, rcvid);
            }

        /* ── 'o' : end-user operation request ── */
        } else if (msg.type == 'o') {
            printf("Server: request from euc: %d %c %d\n", msg.arg1, msg.oper, msg.arg2);

            int slot = find_worker(msg.oper);
            if (slot == -1) {
                my_msg_t err = msg;
                err.type = 'e';
                MsgReply(rcvid, EOK, &err, sizeof(err));

            } else if (regs[slot].pulse_coid != 0) {
                /* ── Pulse-driven worker ── */
                if (regs[slot].pending_euc != -1) {
                    my_msg_t err = msg;
                    err.type = 'e';
                    MsgReply(rcvid, EOK, &err, sizeof(err));
                } else {
                    regs[slot].pending_euc = rcvid;
                    if (MsgSendPulse(regs[slot].pulse_coid, -1,
                                     MY_PULSE_CODE, 0) == -1) {
                        perror("MsgSendPulse to pulse client");
                        my_msg_t err = msg;
                        err.type = 'e';
                        MsgReply(rcvid, EOK, &err, sizeof(err));
                        regs[slot].pending_euc = -1;
                    } else {
                        printf("Server: pulsed '%c' worker, euc rcvid=%d saved\n",
                               msg.oper, rcvid);
                    }
                }

            } else if (regs[slot].rcvid_wc > 0) {
                /* ── Reply-driven worker (idle) ── */
                my_msg_t job = msg;
                job.type      = 'o';
                job.rcvid_euc = rcvid;
                int wc_rcvid  = regs[slot].rcvid_wc;
                regs[slot].rcvid_wc = -1;
                MsgReply(wc_rcvid, EOK, &job, sizeof(job));

            } else {
                /* worker registered but busy */
                my_msg_t err = msg;
                err.type = 'e';
                MsgReply(rcvid, EOK, &err, sizeof(err));
            }

        /* ── 'a' : worker answer ── */
        } else if (msg.type == 'a') {
            printf("Server: answer for '%c': result=%d\n", msg.oper, msg.result);

            int slot = find_reg(msg.oper);
            if (slot != -1 && regs[slot].pulse_coid != 0) {
                /* ── Pulse-driven answer ── */
                if (regs[slot].pending_euc != -1) {
                    my_msg_t reply = msg;
                    reply.type = 'a';
                    MsgReply(regs[slot].pending_euc, EOK, &reply, sizeof(reply));
                    regs[slot].pending_euc = -1;
                }
                MsgReply(rcvid, EOK, NULL, 0);

            } else {
                /* ── Reply-driven answer ── */
                MsgReply(msg.rcvid_euc, EOK, &msg, sizeof(msg));
                if (slot != -1)
                    regs[slot].rcvid_wc = rcvid;
            }

        /* ── 'e' : worker error (unchanged) ── */
        } else if (msg.type == 'e') {
            printf("Server: error from worker: %c (divide by 0)\n", msg.oper);
            MsgReply(msg.rcvid_euc, EOK, &msg, sizeof(msg));
        }
    }

    name_detach(attach, 0);
    printf("Server exit.\n");
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
    char 			  worker_type = 'g';

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
