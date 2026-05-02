/*
 * ex3_server.c — same reply-driven IPC as before, extended for sin client.
 *
 * NEW: sin worker ('s') registers differently:
 *   - It sends type 'r' / oper 's' plus its chid.
 *   - Server opens a connection to the sin client's chid and stores it.
 *   - Server replies immediately (EOK, empty) instead of keeping the rcvid.
 *   - When an euc requests 's', the server sends MY_PULSE_CODE to the sin
 *     client and saves the euc's rcvid. The sin client later sends 'a' back;
 *     the server forwards the result to the euc.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/dispatch.h>
#include "common.h"

typedef struct registration {
    char oper;
    int  rcvid_wc;       /* >0 idle, -1 busy, 0 free — same as before   */
    int  sin_coid;       /* *** NEW *** coid to pulse sin worker (oper=='s' only) */
} registration_t;

#define MAX_WORKERS 1024
registration_t regs[MAX_WORKERS];
int nregs = 0;

/* *** NEW *** pending euc rcvid waiting for sin result (-1 = none) */
static int pending_sin_euc = -1;

static int find_worker(char oper) {
    int i;
    for (i = 0; i < nregs; i++)
        if (regs[i].oper == oper && regs[i].rcvid_wc != -1)
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

    printf("Server starting...\n");

    if ((attach = name_attach(NULL, ATTACH_POINT, 0)) == NULL) {
        perror("name_attach"); return EXIT_FAILURE;
    }
    printf("name_attach OK, registered as '%s'\n", ATTACH_POINT);

    while (1) {
        rcvid = MsgReceive(attach->chid, &msg, sizeof(msg), NULL);
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
            printf("Server: worker registered for '%c' (rcvid=%d)\n", msg.oper, rcvid);

            /* *** NEW *** sin client registers differently */
            if (msg.oper == 's') {
                /*
                 * Check for duplicate sin worker.
                 */
                int i, found = -1;
                for (i = 0; i < nregs; i++) {
                    if (regs[i].oper == 's') { found = i; break; }
                }
                if (found != -1 && regs[found].sin_coid != 0) {
                    /* already have one, reject */
                    MsgError(rcvid, EBADSLT);
                } else {
                    /* Open a connection to the sin client's channel */
                    int sc = ConnectAttach(0, 0, msg.chid, _NTO_SIDE_CHANNEL, 0);
                    if (sc == -1) {
                        perror("ConnectAttach to sin client");
                        MsgError(rcvid, errno);
                    } else {
                        if (found == -1) {
                            found = nregs++;
                            regs[found].oper    = 's';
                            regs[found].rcvid_wc = 1; /* mark as "available" */
                        }
                        regs[found].sin_coid = sc;
                        /*
                         * Reply immediately — sin client must NOT stay blocked.
                         * It needs to return to its timer/pulse loop.
                         */
                        MsgReply(rcvid, EOK, NULL, 0);
                        printf("Server: sin worker registered, sin_coid=%d\n", sc);
                    }
                }
            } else {
                /* ── arithmetic worker registration (unchanged logic) ── */
                int slot = -1, i, error = 0;
                for (i = 0; i < nregs; i++) {
                    if (regs[i].rcvid_wc != 0 && regs[i].oper == msg.oper) {
                        error = 1; break;
                    } else if (regs[i].rcvid_wc == 0 && regs[i].oper == msg.oper) {
                        regs[i].rcvid_wc = rcvid; error = 2; break;
                    }
                }
                if (error == 0 && slot == -1 && nregs < MAX_WORKERS) {
                    slot = nregs++;
                    regs[slot].oper = msg.oper;
                }
                if (error == 0 && slot != -1)
                    regs[slot].rcvid_wc = rcvid;   /* worker stays blocked */
                else if (error == 2)
                    continue;                        /* slot refreshed, do nothing */
                else
                    MsgError(rcvid, EBADSLT);        /* duplicate, reject */
            }

        /* ── 'o' : end-user operation request ── */
        } else if (msg.type == 'o') {
            printf("Server: request from euc: %d %c %d\n", msg.arg1, msg.oper, msg.arg2);

            /* *** NEW *** handle sin operation via pulse */
            if (msg.oper == 's') {
                int i, found = -1;
                for (i = 0; i < nregs; i++)
                    if (regs[i].oper == 's' && regs[i].sin_coid != 0) { found = i; break; }

                if (found == -1) {
                    /* no sin worker registered */
                    my_msg_t err = msg;
                    err.type = 'e';
                    MsgReply(rcvid, EOK, &err, sizeof(err));
                } else if (pending_sin_euc != -1) {
                    /* already one euc waiting for sin — busy */
                    my_msg_t err = msg;
                    err.type = 'e';
                    MsgReply(rcvid, EOK, &err, sizeof(err));
                } else {
                    /*
                     * Save the euc's rcvid, then pulse the sin client.
                     * The euc stays BLOCKED until we get the 'a' back.
                     */
                    pending_sin_euc = rcvid;
                    if (MsgSendPulse(regs[found].sin_coid, -1, MY_PULSE_CODE, 0) == -1) {
                        perror("MsgSendPulse to sin client");
                        my_msg_t err = msg;
                        err.type = 'e';
                        MsgReply(rcvid, EOK, &err, sizeof(err));
                        pending_sin_euc = -1;
                    } else {
                        printf("Server: pulsed sin client, euc rcvid=%d saved\n", rcvid);
                        /* euc stays blocked here — 'a' from sin client will unblock it */
                    }
                }
            } else {
                /* ── arithmetic operation (unchanged) ── */
                int slot = find_worker(msg.oper);
                if (slot == -1 || regs[slot].rcvid_wc == 0) {
                    my_msg_t job = msg;
                    job.type = 'e';
                    MsgReply(rcvid, EOK, &job, sizeof(job));
                } else {
                    my_msg_t job = msg;
                    job.type      = 'o';
                    job.rcvid_euc = rcvid;
                    int wc_rcvid  = regs[slot].rcvid_wc;
                    regs[slot].rcvid_wc = -1;
                    MsgReply(wc_rcvid, EOK, &job, sizeof(job));
                    /* euc stays blocked */
                }
            }

        /* ── 'a' : worker answer ── */
        } else if (msg.type == 'a') {

            /* *** NEW *** sin client sends 'a' back as a MsgSend */
            if (msg.oper == 's') {
                printf("Server: sin answer = %d (x1000)\n", msg.result);
                if (pending_sin_euc != -1) {
                    my_msg_t reply = msg;
                    reply.type = 'a';
                    MsgReply(pending_sin_euc, EOK, &reply, sizeof(reply));
                    pending_sin_euc = -1;
                }
                /* Ack the sin client's MsgSend so it can return to its loop */
                MsgReply(rcvid, EOK, NULL, 0);

            } else {
                /* ── arithmetic answer (unchanged) ── */
                printf("Server: answer from worker: result=%d\n", msg.result);
                MsgReply(msg.rcvid_euc, EOK, &msg, sizeof(msg));
                int i;
                for (i = 0; i < nregs; i++) {
                    if (regs[i].oper == msg.oper) {
                        regs[i].rcvid_wc = rcvid;
                        break;
                    }
                }
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