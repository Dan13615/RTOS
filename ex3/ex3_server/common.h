/*
 * common.h
 *
 *  Created on: 26-03-2026
 *      Author: lab-user
 */

#ifndef COMMON_H_
    #define COMMON_H_
    #include <sys/dispatch.h>

    #define ATTACH_POINT "calc_svr"
	/*
	 * Message types:
	 *   'o' - operation (euc -> server)
	 *   'r' - register worker (wc  -> server)
	 *   'a' - answer (server -> wc, carrying result back)
	 */

	typedef struct _pulse msg_header_t;

	typedef struct my_msg {
		msg_header_t hdr;
		char  type;         /* 'o' | 'r' | 'a' */
		char  oper;         /* '+' | '-' | '*' | '/' */
		int   arg1;
		int   arg2;
		int   result;
		int   rcvid_euc;
	} my_msg_t;

	extern int server_coid;

#endif /* COMMON_H_ */

