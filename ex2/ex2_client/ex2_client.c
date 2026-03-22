#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "common.h"
#include <unistd.h>

int main(int argc, char* argv[]) {
    my_data_t msg;
    int server_coid;

    if ((server_coid = name_open(ATTACH_POINT, 0)) == -1) {
        return EXIT_FAILURE;
    }
    int pid = getpid();

    printf("name_open succeeded, my pid=%d\n", pid);

    msg.hdr.type    = 0x00;
    msg.hdr.subtype = 0x00;

    while (1) {
        printf("Enter a word: ");

        if (fgets(msg.data, 256, stdin) == NULL) {
            break;
        }

        size_t len = strlen(msg.data);
        if (len > 0 && msg.data[len - 1] == '\n') {
            msg.data[--len] = '\0';
        }

        int is_exit = (strcmp(msg.data, "exit") == 0);

        printf("- calling MsgSend..\n");

        int status = MsgSend(server_coid, &msg, sizeof(msg), &msg, sizeof(msg));
        if (status == -1) {
            break;
        }

        printf("- msgReply.word = %s, status=%d\n", msg.data, status);

        if (is_exit) {
            printf("Client exit.\n");
            break;
        }
    }

    name_close(server_coid);
    return EXIT_SUCCESS;
}
