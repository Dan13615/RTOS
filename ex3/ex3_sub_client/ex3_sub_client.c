/*
 * ex3_sub_client.c - worker client for the '-' operator.
 *
 * Same pattern as every worker:
 *   1. name_open to find the server.
 *   2. First MsgSend is a registration ('r') - stays blocked until
 *      the server hands us a job.
 *   3. Compute, then loop: the next MsgSend carries the answer ('a')
 *      and also acts as "ready for next job" because we block on it.
 *
 * Only one '-' worker can be alive at once (server enforces it).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/dispatch.h>
#include "common.h"

int server_coid = -1;

int main(int argc, char *argv[]) {
    char oper = '-';

    printf("Worker client '%c' starting...\n", oper);

    server_coid = name_open(ATTACH_POINT, 0);
    if (server_coid == -1) {
        perror("name_open");
        return EXIT_FAILURE;
    }
    printf("Worker '%c': connected to server (coid=%d)\n", oper, server_coid);

    my_msg_t send_msg, recv_msg;
    memset(&send_msg, 0, sizeof(send_msg));

    /* First send: registration. Server will keep us blocked on this
     * MsgSend until a subtraction job is routed to us. */
    send_msg.type = 'r';
    send_msg.oper = oper;

    while (1) {
        int status = MsgSend(server_coid, &send_msg, sizeof(send_msg), &recv_msg, sizeof(recv_msg));
        if (status == -1) {
            perror("MsgSend");
            break;
        }

        if (recv_msg.type != 'o') {
            fprintf(stderr, "Worker '%c': unexpected msg type '%c'\n", oper, recv_msg.type);
            break;
        }

        /* Job received: compute and prepare the answer message.
         * rcvid_euc is forwarded so the server can route the reply
         * back to the exact end-user client that asked. */
        int a = recv_msg.arg1, b = recv_msg.arg2;
        int result = a - b;
        printf("Worker '%c': %d %c %d = %d\n", oper, a, oper, b, result);

        send_msg.type      = 'a';
        send_msg.oper      = oper;
        send_msg.arg1      = a;
        send_msg.arg2      = b;
        send_msg.result    = result;
        send_msg.rcvid_euc = recv_msg.rcvid_euc;
    }

    name_close(server_coid);
    printf("Worker '%c' exit.\n", oper);
    return EXIT_SUCCESS;
}
