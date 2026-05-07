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
