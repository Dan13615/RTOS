#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/dispatch.h>
#include "common.h"


typedef struct registration {
    char oper;
    int  rcvid_wc;
} registration_t;

#define MAX_WORKERS 1024
registration_t regs[MAX_WORKERS];
int nregs = 0;

static int find_worker(char oper) {
	int i = 0;
    for (i = 0; i < nregs; i++) {
        if (regs[i].oper == oper && regs[i].rcvid_wc != -1)
            return i;
    }
    return -1;
}

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

    if ((attach = name_attach(NULL, ATTACH_POINT, 0)) == NULL) {
        perror("name_attach");
        return EXIT_FAILURE;
    }
    printf("name_attach OK, registered as '%s'\n", ATTACH_POINT);

    while (1) {
        rcvid = MsgReceive(attach->chid, &msg, sizeof(msg), NULL);

        if (rcvid == -1) {
            perror("MsgReceive");
            break;
        }

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

        if (msg.type == 'r') {
            /*
             * Worker client registering.
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
             * End-user client requesting an operation.
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
             * Worker has finished
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
            printf("Server: error from worker: %c result=%d (divide by 0)\n", msg.oper, msg.result);
            MsgReply(msg.rcvid_euc, EOK, &msg, sizeof(msg));
        }
    }

    name_detach(attach, 0);
    printf("Server exit.\n");
    return EXIT_SUCCESS;
}
