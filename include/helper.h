#ifndef HELPER_H
#define HELPER_H

#include "server.h"
#include "debug.h"
#include "protocol.h"
#include "dlinkedlist.h"

#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>

typedef struct {
    uint32_t enrolled;
    uint32_t waitlisted;
} enrollment_t;

typedef struct {
    char* username;
    int socket_fd;
    pthread_t tid;
    enrollment_t courses;
    pthread_mutex_t user_lock;      //ADDED A LOCK ATTRIBUTE
    pthread_mutex_t read_lock;
    int read_count;
} user_t;

typedef struct {
    char* title;
    int   maxCap;
    dlist_t enrollment;
    dlist_t waitlist;
} course_t;

extern int total_courses;
extern volatile sig_atomic_t sigint_flag;

int comparatorFunc(const void * a, const void * b);

void printerFunc(void * a, void * b);

void deleterFunc(void * a);

void rm_and_delete_node(dlist_t* users, void *a);

int server_startup(int port_number, char * course_filename, char * log_filename);

int populate_courses(char * course_file, course_t * courseArray);

void sigint_handler(int sig);

void init_server_stats_locks(pthread_mutex_t * num_accepted_connections_lock, pthread_mutex_t * sucessful_client_logins_lock, pthread_mutex_t * num_attempted_enrollments_lock, pthread_mutex_t * num_successful_drops_lock, pthread_mutex_t * enrollment_log_lock);

void init_course_array_locks(pthread_mutex_t * locks_array);

void init_user_locks(dlist_t * usr_head);

void *client_work(void* clientfd_ptr);

void print_linked_list(dlist_t * user_LL, FILE * fp, char * buf);
#endif

