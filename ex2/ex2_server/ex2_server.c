#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "common.h"
#include <unistd.h>

int main(int argc, char *argv[]) {
    name_attach_t *attach;
    my_data_t      msg;
    int            rcvid;

    printf("Server application is starting...\n");

    if ((attach = name_attach(NULL, ATTACH_POINT, 0)) == NULL) {
        return EXIT_FAILURE;
    }

    int pid = getpid();

    printf("- name_attach succeeded, my pid=%d\n", pid);

    while (1) {
        printf("MsgReceive is waiting for a message...\n");
        rcvid = MsgReceive(attach->chid, &msg, sizeof(msg), NULL);

        if (rcvid == -1) {
            break;
        }

        if (rcvid == 0) { /* Pulse received */
            switch (msg.hdr.code) {
            case _PULSE_CODE_DISCONNECT:
                ConnectDetach(msg.hdr.scoid);
                break;
            case _PULSE_CODE_UNBLOCK:
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

        int is_exit = (strcmp(msg.data, "exit") == 0);
        int len     = (int)strlen(msg.data);

        char reversed[256];
        int i;
        for (i = 0; i < len; i++) {
            reversed[i] = msg.data[len - 1 - i];
        }
        reversed[len] = '\0';

        snprintf(msg.data, 256, "%d:%s", len, reversed);

        printf("- received word, replying with %s\n", msg.data);

        MsgReply(rcvid, len, &msg, sizeof(msg));

        if (is_exit) {
            break;
        }
    }

    name_detach(attach, 0);
    printf("Server exit.\n");

    return EXIT_SUCCESS;
}
