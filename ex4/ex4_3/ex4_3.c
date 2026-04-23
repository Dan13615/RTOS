#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <sys/netmgr.h>
#include <sys/neutrino.h>

#define PULSE_TIMER1   _PULSE_CODE_MINAVAIL
#define PULSE_TIMER2    _PULSE_CODE_MAXAVAIL

typedef union {
    struct _pulse   pulse;
} my_message_t;

int main()
{
    struct sigevent event1;
    struct sigevent event2;
    struct itimerspec itime1;
    struct itimerspec itime2;
    timer_t timer_id1;
    timer_t timer_id2;
    int chid;

    rcvid_t rcvid;
    my_message_t msg;
    struct sched_param scheduling_params;
    int prio;

    chid = ChannelCreate(0);

    if (SchedGet( 0, 0, &scheduling_params) != -1) {
        prio = scheduling_params.sched_priority;
    } else {
        prio = 10;
    }

    int coid = ConnectAttach(0, 0, chid, _NTO_SIDE_CHANNEL, 0);
    SIGEV_PULSE_INIT(&event1, coid, prio, PULSE_TIMER1, 0);
    SIGEV_PULSE_INIT(&event2, coid, prio, PULSE_TIMER2, 0);

    timer_create(CLOCK_MONOTONIC, &event1, &timer_id1);
    timer_create(CLOCK_MONOTONIC, &event2, &timer_id2);

    itime1.it_value.tv_sec = 0;
    itime1.it_value.tv_nsec = 10000000;
    itime1.it_interval.tv_sec = 0;
    itime1.it_interval.tv_nsec = 10000000;
    itime2.it_value.tv_sec = 10;
    itime2.it_value.tv_nsec = 0;
    itime2.it_interval.tv_sec = 10;
    itime2.it_interval.tv_nsec = 0;
    timer_settime(timer_id1, 0, &itime1, NULL);
    timer_settime(timer_id2, 0, &itime2, NULL);

    int i = 0, j = 0;

    for (;;) {
        rcvid = MsgReceive(chid, &msg, sizeof(msg), NULL);
        if (rcvid == -1) {
            perror("MsgReceive");
            break;
        }
        if (rcvid == 0) { 
            if (msg.pulse.code == PULSE_TIMER1) {
                i++;
                printf("we got a pulse from our timer1 after 10 ms, total pulses: %d\n", i);
            } else if (msg.pulse.code == PULSE_TIMER2) {
                j++;
                printf("we got a pulse from our timer2 after 10 seconds, total pulses: %d\n", j);
                return(0);
            } 
        } 
    }
    return(1);
}