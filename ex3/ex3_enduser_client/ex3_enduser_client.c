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

    server_coid = name_open(ATTACH_POINT, 0);
    if (server_coid == -1) {
        perror("name_open – is the server running?");
        return EXIT_FAILURE;
    }
    printf("Connected to calc_svr (coid=%d)\n", server_coid);

    while (1) {
        printf("Enter [number] [operator] [number]: ");
        fflush(stdout);
        argsNum = scanf("%d %c %d", &num1, &operation, &num2);

        if (argsNum != 3) {
            printf("Bad input, try again.\n");
            while ((c = getchar()) != '\n' && c != EOF);
            continue;
        }
        if (operation == 'q')
            break;
        if (strchr("+-*/", operation) == NULL) {
            printf("Operation '%c' not supported. Use + - * /\n", operation);
            continue;
        }

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
