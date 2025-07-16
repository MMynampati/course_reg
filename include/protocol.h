#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

enum msg_types {
    OK,
    LOGIN,
    LOGOUT,
    CLIST,
    SCHED,
    ENROLL,
    DROP,
    WAIT,
    EUSRLGDIN = 0xF0,
    ECDENIED,
    ECNOTFOUND,
    ENOCOURSES,
    ESERV = 0xFF
};

typedef struct {
    uint32_t msg_len; 
    uint8_t msg_type; 
} petrV_header;

int rd_msgheader(int socket_fd, petrV_header *h);
int wr_msg(int socket_fd, petrV_header *h, char *msgbuf);

#endif