#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/dispatch.h>
#include <math.h>
#include "common.h"

int server_coid = -1;

#define TIMER_PULSE   _PULSE_CODE_MAXAVAIL


int main(int argc, char *argv[]) {
	struct sigevent event;
	struct itimerspec itime;
	timer_t timer_id;
	int chid;
	int rcvid;
	struct sched_param scheduling_params;
	int prio;
	int sinT;
	int timer_count = 0;

	chid = ChannelCreate(0);

	if (SchedGet( 0, 0, &scheduling_params) != -1) {
		prio = scheduling_params.sched_priority;
	} else {
		prio = 10;
	}

	int coid = ConnectAttach(0, 0, chid, _NTO_SIDE_CHANNEL, 0);
	SIGEV_PULSE_INIT(&event, coid, prio, MY_PULSE_CODE, 0);

	timer_create(CLOCK_MONOTONIC, &event, &timer_id);

	itime.it_value.tv_sec = 0;
	itime.it_value.tv_nsec = 100000000;
	itime.it_interval.tv_sec = 0;
	itime.it_interval.tv_nsec = 100000000;
	timer_settime(timer_id, 0, &itime, NULL);

	// -----------------------------------------------

    char oper = 's';
    my_pulse_t recv_pulse;

    printf("Worker client '%c' starting...\n", oper);

    server_coid = name_open(ATTACH_POINT, 0);
    if (server_coid == -1) {
        perror("name_open");
        return EXIT_FAILURE;
    }
    printf("Worker '%c': connected to server (coid=%d)\n", oper, server_coid);

    my_msg_t send_msg, recv_msg;
    memset(&send_msg, 0, sizeof(send_msg));

    send_msg.type = 'R';
    send_msg.oper = oper;

    int status = MsgSend(server_coid, &send_msg, sizeof(send_msg), &recv_msg, sizeof(recv_msg));
	if (status == -1) {
		perror("MsgSend");
		return EXIT_FAILURE;
	}

	while (1) {
		rcvid = MsgReceivePulse(server_coid, &recv_pulse, sizeof(recv_pulse), NULL);

		if (rcvid == -1) {
			perror("MsgReceivePulse");
			break;
		}

		if (rcvid == 0) {
			int id = 0;
			switch (msg.pulse.code) {
				case _MY_PULSE_CODE:
					MsgSendPulse(server_coid, 1, MY_PULSE_CODE, sinT);
					break;
				case TIMER_PULSE:
					timer_count++;
					sinT = sin(timer_count);
					break;
				default:
					break;
			}
			   continue;
		}
	}



    name_close(server_coid);
    printf("Worker '%c' exit.\n", oper);
    return EXIT_SUCCESS;
}
