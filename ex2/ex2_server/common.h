#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dispatch.h>

#define ATTACH_POINT "myname"

typedef struct _pulse msg_header_t;

typedef struct _my_data {
    msg_header_t hdr;
    char data[256];
} my_data_t;
