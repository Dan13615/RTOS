/*
 * ex3_server.c - central dispatcher for the calculator system.
 *
 * Role:
 *   - Registers itself under a QNX name (ATTACH_POINT) so clients can find it.
 *   - Keeps a table of worker clients (one per operator: +, -, *, /).
 *   - Receives requests from end-user clients (euc) and forwards the job
 *     to the correct worker, then relays the worker's answer back to the euc.
 *
 * Key idea: the worker stays BLOCKED on MsgSend until a job arrives. The
 * server answers the MsgSend only when it has work for it (acts as a queue
 * of one slot per operator). The euc also stays blocked until the worker
 * produced a result - the server replies to the euc with the final answer.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/dispatch.h>
#include "common.h"


/*
 * One entry per registered worker.
 *  - oper      : the operator the worker handles ('+', '-', '*', '/').
 *  - rcvid_wc  : the pending rcvid of the worker's MsgSend. Values:
 *                >0  : worker is blocked, waiting for a job (usable slot)
 *                 0  : slot is free (no worker, or worker just finished)
 *                -1  : worker is currently busy doing a job
 */
typedef struct registration {
    char oper;
    int  rcvid_wc;
} registration_t;

#define MAX_WORKERS 1024
registration_t regs[MAX_WORKERS];
int nregs = 0;

/* Look up an idle worker for a given operator. Returns index or -1. */
static int find_worker(char oper) {
	int i = 0;
    for (i = 0; i < nregs; i++) {
        if (regs[i].oper == oper && regs[i].rcvid_wc != -1)
            return i;
    }
    return -1;
}

/*
 * Called when the kernel tells us a worker has died / disconnected.
 * We mark the matching slot as free (rcvid_wc = 0) so a new worker
 * can take over that operator later. The dead rcvid is wipe it from the table.
 */
void rcvid_to_null(int rcvid) {
	int i = 0;
    for (i = 0; i < nregs; i++) {
        if (regs[i].rcvid_wc == rcvid && regs[i].rcvid_wc != -1)
            regs[i].rcvid_wc = 0;
    }
}

int main(void) {
    name_attach_t *attach;
    my_msg_t          msg;
    int            rcvid;

    printf("Server starting...\n");

    /* Publish the server under ATTACH_POINT so clients find it by name. */
    if ((attach = name_attach(NULL, ATTACH_POINT, 0)) == NULL) {
        perror("name_attach");
        return EXIT_FAILURE;
    }
    printf("name_attach OK, registered as '%s'\n", ATTACH_POINT);

    /*
     * Main server loop. MsgReceive blocks until something arrives:
     *   - rcvid == 0 : a PULSE (kernel notification, e.g. a client died)
     *   - rcvid >  0 : a real message from a worker or end-user client
     */
    while (1) {
        rcvid = MsgReceive(attach->chid, &msg, sizeof(msg), NULL);

        if (rcvid == -1) {
            perror("MsgReceive");
            break;
        }

        /*
         * Pulse handling.
         * QNX sends us special pulses when a client dies or unblocks:
         *   _PULSE_CODE_DISCONNECT : the client's connection is gone
         *                            (process exited / crashed).
         *   _PULSE_CODE_UNBLOCK    : the client aborted its MsgSend
         *                            (e.g. Ctrl-C while blocked).
         * In both cases we must detach the connection and clean the
         * worker table force not receive from a dead rcvid.
         */
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
			   break;
		   default:
			   break;
			   }
		   continue;
		   }

        if (msg.hdr.type == _IO_CONNECT) {
            MsgReply(rcvid, EOK, NULL, 0);
            continue;
        }
        if (msg.hdr.type > _IO_BASE && msg.hdr.type <= _IO_MAX) {
            MsgError(rcvid, ENOSYS);
            continue;
        }

        /*
         * Our custom "protocol" uses msg.type:
         *   'r' : worker registration (new worker for an operator)
         *   'o' : end-user request (do arg1 oper arg2)
         *   'a' : worker returning an answer
         *   'e' : worker returning an error (e.g. div by 0)
         */
        if (msg.type == 'r') {
            /*
             * Worker registration.
             *
             * A worker sends 'r' + its operator and then stays BLOCKED on
             * its MsgSend. We store the rcvid in the table but do NOT
             * reply yet: the worker will only unblock when a real job
             * arrives for it.
             *
             * Rule: only ONE active worker per operator. If another
             * worker tries to register for the same operator while one
             * is already alive, we reject it with EBADSLT.
             */
            printf("Server: worker registered for '%c' (rcvid=%d)\n", msg.oper, rcvid);

            int slot = -1;
            int i = 0;
            int error = 0;
            for (i = 0; i < nregs; i++) {
                if (regs[i].rcvid_wc != 0 && regs[i].oper == msg.oper) {
                	error = 1;
                	break;
                } else if (regs[i].rcvid_wc == 0 && regs[i].oper == msg.oper) {
                	regs[i].rcvid_wc = rcvid;
                	error = 2;
                	break;
                }
            }

            if (error == 0 && slot == -1 && nregs < MAX_WORKERS) {
                slot = nregs++;
                regs[slot].oper = msg.oper;
            }
            if (error == 0 && slot != -1) {
                regs[slot].rcvid_wc = rcvid;
                /* Do NOT reply yet, the worker stays blocked waiting for a job */
            }
            else if (error == 2) {
            	continue;
            } else {
                MsgError(rcvid, EBADSLT);
            }

        } else if (msg.type == 'o') {
            /*
             * End-user client asks for: arg1 <oper> arg2.
             *
             * We look up an idle worker for that operator. If none is
             * available (no worker registered, or the worker is already
             * busy), we immediately answer the euc with type 'e' so it
             * does not hang forever.
             *
             * If a worker is free, we mark its slot busy (-1) and REPLY
             * to the worker's pending MsgSend with the job. The euc is
             * kept blocked - the worker's later answer ('a') will unblock
             * it.
             */
            printf("Server: request from euc: %d %c %d\n", msg.arg1, msg.oper, msg.arg2);

            int slot = find_worker(msg.oper);
            if (slot == -1 || regs[slot].rcvid_wc == 0) {
                printf("Server: no worker available for '%c'\n", msg.oper);
                my_msg_t job = msg;
				job.type = 'e';
				job.rcvid_euc = rcvid;

				MsgReply(rcvid, EOK, &job, sizeof(job));
                continue;
            } else {
                my_msg_t job = msg;
                job.type = 'o';
                job.rcvid_euc = rcvid;

                int wc_rcvid = regs[slot].rcvid_wc;
                regs[slot].rcvid_wc = -1;

                MsgReply(wc_rcvid, EOK, &job, sizeof(job));
                /* The euc stays blocked until the worker sends the answer */
            }
        } else if (msg.type == 'a') {
            /*
             * Worker finished its job.
             *
             * Two things happen here:
             *  1. The Reply to the waiting euc (stored as rcvid_euc in the
             *     message) with the result - this unblocks the user's
             *     MsgSend.
             *  2. Refresh the worker's slot with its NEW rcvid so it can
             *     receive the next job. The worker sent this 'a' message
             *     with a MsgSend, so it is currently blocked again on
             *     that send - we simply keep its rcvid and wait for more
             *     work before replying.
             */
            printf("Server: answer from worker: result=%d\n", msg.result);

            /* Reply to the euc with the finished message */
            MsgReply(msg.rcvid_euc, EOK, &msg, sizeof(msg));

            int slot = find_worker(msg.oper);

            int i = 0;

            for (i = 0; i < nregs; i++) {
                if (regs[i].oper == msg.oper) {
                    regs[i].rcvid_wc = rcvid;
                    break;
                }
            }
        } else if (msg.type == 'e') {
            /*
             * Worker reported a computation error (currently: divide by 0).
             * Forward the error message to the waiting euc so its MsgSend
             * returns and it can display the error to the user.
             */
            printf("Server: error from worker: %c result=%d (divide by 0)\n", msg.oper, msg.result);
            MsgReply(msg.rcvid_euc, EOK, &msg, sizeof(msg));
        }
    }

    name_detach(attach, 0);
    printf("Server exit.\n");
    return EXIT_SUCCESS;
}
