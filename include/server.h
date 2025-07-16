#ifndef SERVER_H
#define SERVER_H

#include <arpa/inet.h>
#include <getopt.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define BUFFER_SIZE 1024
#define SA struct sockaddr

#define USAGE_MSG "./bin/zotReg_server [-h] PORT_NUMBER COURSE_FILENAME LOG_FILENAME"\
                  "\n  -h                 Displays this help menu and returns EXIT_SUCCESS."\
                  "\n  PORT_NUMBER        Port number to listen on."\
                  "\n  COURSE_FILENAME    File to read course information from at the start of the server"\
                  "\n  LOG_FILENAME       File to output server actions into. Create/overwrite, if exists\n"


// INSERT HELPER FUNCTIONS HERE

#endif
