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
        if (strchr("+-*/s", operation) == NULL) {
            printf("Operation '%c' not supported. Use + - * / s\n", operation);
            continue;
        }

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
