/*
 * ex3_add_client.c - worker client for the '+' operator.
 *
 * Idea:
 *   1. Open a connection to the server by name.
 *   2. Send a registration message (type 'r', oper '+') and stay BLOCKED
 *      on that MsgSend. The server holds our rcvid and replies only when
 *      a real job arrives, so MsgSend acts as a "wait for a job" call.
 *   3. When we unblock, recv_msg contains a job (type 'o'): do the math
 *      and loop back with a new MsgSend of type 'a' (answer). That next
 *      MsgSend doubles as "here is the result" AND "I am ready again".
 *
 * Only one '+' worker can run at a time - the server refuses duplicates.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/dispatch.h>
#include "common.h"

int server_coid = -1;

int main(int argc, char *argv[]) {
    char oper = '+';

    printf("Worker client '%c' starting...\n", oper);

    server_coid = name_open(ATTACH_POINT, 0);
    if (server_coid == -1) {
        perror("name_open");
        return EXIT_FAILURE;
    }
    printf("Worker '%c': connected to server (coid=%d)\n", oper, server_coid);

    my_msg_t send_msg, recv_msg;
    memset(&send_msg, 0, sizeof(send_msg));

    /* First MsgSend is the registration ('r'). Later iterations will
     * reuse send_msg with type 'a' to send back results. */
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

        /* Do the work, then prepare the next MsgSend as an 'a' (answer).
         * rcvid_euc is copied through so the server knows which end-user
         * client is waiting for this result. */
        int a = recv_msg.arg1, b = recv_msg.arg2;
        int result = a + b;
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
