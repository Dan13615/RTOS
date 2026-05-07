#ifndef COMMON_H_
    #define COMMON_H_
    #include <sys/dispatch.h>

    #define ATTACH_POINT "calc_svr"
    #define MY_PULSE_CODE _PULSE_CODE_MINAVAIL  /* server->sin_client: "give me sin now" */

    typedef struct _pulse msg_header_t;

    typedef struct my_msg {
        msg_header_t hdr;
        char  type;       /* 'o' | 'r' | 'a' | 'e' | 's' */
        char  oper;       /* '+' | '-' | '*' | '/' | 's'  */
        int   arg1;
        int   arg2;
        int   result;
        int   rcvid_euc;
        int   chid;       /* sin_client fills this on 'r' so server can pulse it */
    } my_msg_t;

    typedef struct my_pulse {
        msg_header_t hdr;
    } my_pulse_t;

    extern int server_coid;

#endif /* COMMON_H_ */