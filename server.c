#include "server.h"
#include "protocol.h"
#include "dlinkedlist.h"
#include "mlinkedlist.h"
#include <pthread.h>
#include <signal.h

#include "helper.h"

/**************FIELDS CAN BE ADDED TO STRUCTS, BUT NOT RENAMED OR REMOVED **********/
//CHANGELOG
    //MOVED THESE TO HELPER.H
    //ADDED A USER_LOCK ATTRIBUTE TO USER_T STRUCT
/*
typedef struct {
    uint32_t enrolled;	
    uint32_t waitlisted;	
} enrollment_t;

typedef struct {
    char* username;	
    int socket_fd;	
    pthread_t tid;
    enrollment_t courses;
} user_t;

typedef struct {
    char* title; 
    int   maxCap;      
    dlist_t enrollment; 
    dlist_t waitlist;   
} course_t; 
*/
/**************DECLARE ALL GLOBAL VARIABLES HERE BETWEEN THES LINES FOR MANUAL GRADING**********/
// COMMENT ON THE PURPOSE OF EACH VARIABLE

course_t courseArray[32];           //array of all courses
int num_accepted_connections = 0;   //total connections accepted by server
int successful_client_logins = 0;   //successful login attempts counter
int num_attempted_enrollments = 0;  //total enrollment attempts counter  
int num_successful_drops = 0;       //successful course drops counter

dlist_t * users = NULL;             //linked list of all users (active and inactive)
char buffer[BUFFER_SIZE];           //general purpose buffer for receiving data

FILE * log_file = NULL;             //global log_file so it can be accessed outside of main func 

//struct to pass multiple args to client thread
typedef struct{
    int client_ptr;
    user_t * user_struct_ptr;
}args_t;

/***********************************************************************************************/

/**********************DECLARE ALL LOCKS HERE BETWEEN THES LINES FOR MANUAL GRADING*************/
// COMMENT ON THE VARIABLE/DATA STRUCTURE USED FOR & PURPOSE OF EACH LOCK
//lock server stats
pthread_mutex_t num_accepted_connections_lock;
pthread_mutex_t successful_client_logins_lock;
pthread_mutex_t num_attempted_enrollments_lock;
pthread_mutex_t num_successful_drops_lock;

//lock enrollment log 
pthread_mutex_t enrollment_log_lock;

//added a user_lock attribute to user_t struct to lock individual user_t structs in the users linked list

//lock each index of courseArray
pthread_mutex_t courseArrayLocks[32];

//all locks initialized on server startup 

/***********************************************************************************************/

//main function for each client thread - handles all client requests
void * client_work(void * arguments){
    args_t * args = (args_t *)arguments;
    int client_fd = args->client_ptr;
    user_t * user = args->user_struct_ptr;
    free(args);      //cleanup thread args immediately
    
    //allocate headers for receiving and sending messages
    petrV_header * header = calloc(1, sizeof(petrV_header));
    petrV_header * send_header = calloc(1, sizeof(petrV_header));
    if(!header || !send_header){
        if(header){free(header);}
        if(send_header){free(send_header);}
        close(client_fd);
        return NULL;
    }

    //init reader-writer lock for this user
    pthread_mutex_init(&user->read_lock, NULL);

    //main client request processing loop
    while(sigint_flag != 1){
         //read header and determine msg type
         //do actions
         //send back confirmation message 
         
         char in_buffer[BUFFER_SIZE];
         bzero(in_buffer, BUFFER_SIZE);
         char out_buffer[BUFFER_SIZE];
         bzero(out_buffer, BUFFER_SIZE);

         //wait for client message
         int ret = rd_msgheader(client_fd, header);
         if(sigint_flag == 1){
            break;
         }

         if(ret == -1){
            printf("reading header failed\n");
            break;
         }
      
        //handle logout 
        if(header->msg_type == LOGOUT){
            pthread_mutex_lock(&user->user_lock);
            user->socket_fd = -1;
            user->tid = 0;
            pthread_mutex_unlock(&user->user_lock);
            
            //log logout event
            pthread_mutex_lock(&enrollment_log_lock);
            fprintf(log_file, "%s LOGOUT\n", user->username);
            fflush(log_file);
            pthread_mutex_unlock(&enrollment_log_lock);
            
            //send confirmation back to client
            send_header->msg_type = OK;     //send OK message back
            send_header->msg_len = 0;
            int ret = wr_msg(client_fd, send_header, (char*)NULL);
            if(ret == -1){
                printf("Error sending message to client\n");
                break;
            }
            break;  //exit client thread
         }
    
        //handle course list request
        if(header->msg_type == CLIST){
            int i = 0;
            int buf_length = 0;
            //iterate through all courses and build response
            while(i < 32){
                pthread_mutex_lock(&courseArrayLocks[i]);
                course_t * course = &courseArray[i];
                if(course->title == NULL){           //if at end of available courses break out of loop
                    pthread_mutex_unlock(&courseArrayLocks[i]);  // UNLOCK before breaking!
                    break;
                }
                int course_enrollment = course->enrollment.length;
                
                //check if course is full and mark as closed
                if(course_enrollment == course->maxCap){
                    buf_length += snprintf(out_buffer + buf_length, BUFFER_SIZE - buf_length, "Course %d - %s (CLOSED)\n", i, course->title); 
                    printf("Course %d - %s (CLOSED)\n", i, course->title);
                }
                else{
                    buf_length += snprintf(out_buffer + buf_length, BUFFER_SIZE - buf_length, "Course %d - %s\n", i, course->title);
                    printf("Course %d - %s\n", i, course->title);
                }
                pthread_mutex_unlock(&courseArrayLocks[i]);
                i++;
            }
            
            //send course list back to client
            send_header->msg_type = CLIST;
            send_header->msg_len = buf_length;
            int ret = wr_msg(client_fd, send_header, out_buffer);
            if(ret == -1){
                printf("Error sending message to client\n");
                break;
            }
            
            //log clist request
            pthread_mutex_lock(&enrollment_log_lock);
            fprintf(log_file, "%s CLIST\n", user->username);
            fflush(log_file);
            pthread_mutex_unlock(&enrollment_log_lock);
            continue;
        }
        
        //handle schedule request
        if(header->msg_type == SCHED){
            int i = 0;
            int buf_length = 0;
            
            //check user's enrollment status
            pthread_mutex_lock(&user->user_lock);             
            int msg = SCHED;
            if(user->courses.enrolled == 0){
                msg = ENOCOURSES;
            }
            else{
                //build schedule from enrolled and waitlisted courses
                while(i<32){
                    //check if user is enrolled in course i
                    if(user->courses.enrolled & (1U << i)){
                        pthread_mutex_lock(&courseArrayLocks[i]);
                        course_t *course = &courseArray[i];
                        buf_length += snprintf(out_buffer + buf_length, BUFFER_SIZE - buf_length, "Course %d - %s\n", i, course->title    );
                        pthread_mutex_unlock(&courseArrayLocks[i]);
                    }
                    //check if user is waitlisted for course i
                    if(user->courses.waitlisted & (1U << i)){
                        pthread_mutex_lock(&courseArrayLocks[i]);
                        course_t *course = &courseArray[i];
                        buf_length += snprintf(out_buffer + buf_length, BUFFER_SIZE - buf_length, "Course %d - %s (WAITING)\n", i, course->title);
                        pthread_mutex_unlock(&courseArrayLocks[i]);
                    }
                    i++;
                }
            }

            pthread_mutex_unlock(&user->user_lock);
            
            //send appropriate response based on whether user has courses
            if(msg==SCHED){
                send_header->msg_type = msg;
                send_header->msg_len = buf_length;
                int ret = wr_msg(client_fd, send_header, out_buffer);
            }
            else if(msg==ENOCOURSES){
                send_header->msg_type = msg;
                send_header->msg_len = 0;
                int ret = wr_msg(client_fd, send_header, (char*)NULL);
            }

            if(ret == -1){
                printf("Error sending message to client\n");
                break;
            }
            
            //log schedule request
            pthread_mutex_lock(&enrollment_log_lock);
            if(msg == SCHED){
                fprintf(log_file, "%s SCHED\n", user->username);
            }
            else{
                fprintf(log_file, "%s NOSCHED\n", user->username);
            }
            fflush(log_file);
            pthread_mutex_unlock(&enrollment_log_lock);

        }

        //handle course enrollment request
        if(header->msg_type == ENROLL){
            //check if course_index is invalid
            bzero(in_buffer, BUFFER_SIZE);
            //read course index from client
            int received_size = read(client_fd, in_buffer, header->msg_len);
            if(received_size < 0){
                printf("Receiving failed\n");
                break;
            }
            
            pthread_mutex_lock(&num_attempted_enrollments_lock);            //update server stats
                num_attempted_enrollments++;
            pthread_mutex_unlock(&num_attempted_enrollments_lock);

            int course_index = atoi(in_buffer);
            printf("%d\n", course_index);
            //validate course index
            if(course_index < 0 || course_index > 31 || courseArray[course_index].title == NULL){
                send_header->msg_type = ECNOTFOUND;
                send_header->msg_len = 0;
                int ret = wr_msg(client_fd, send_header, (char*)NULL);
                if(ret == -1){
                    printf("Error sending message to client\n");
                    break;
                }
                pthread_mutex_lock(&enrollment_log_lock);
                fprintf(log_file, "%s NOTFOUND_E %d\n", user->username, course_index);
                fflush(log_file);
                pthread_mutex_unlock(&enrollment_log_lock);
                continue;
            }
            else{                               
                pthread_mutex_lock(&user->user_lock);       //check if user is alr enrolled
                if(user->courses.enrolled & (1U << course_index)){
                    pthread_mutex_unlock(&user->user_lock);
                    send_header->msg_type = ECDENIED;
                    send_header->msg_len = 0;
                    int ret = wr_msg(client_fd, send_header, (char*)NULL);
                    if(ret == -1){
                        printf("Error sending message to client\n");
                        break;
                    }
                    pthread_mutex_lock(&enrollment_log_lock);
                    fprintf(log_file, "%s NOENROLL %d\n", user->username, course_index);
                    fflush(log_file);
                    pthread_mutex_unlock(&enrollment_log_lock);
                    continue;
                }
                pthread_mutex_unlock(&user->user_lock);

                //check if course has space
                pthread_mutex_lock(&courseArrayLocks[course_index]);
                if(courseArray[course_index].maxCap == courseArray[course_index].enrollment.length){  //course is full
                    send_header->msg_type = ECDENIED;
                    send_header->msg_len = 0;
                    int ret = wr_msg(client_fd, send_header, (char*)NULL);
                    if(ret == -1){
                        printf("Error sending message to client\n");
                        pthread_mutex_unlock(&courseArrayLocks[course_index]);
                        break;
                    }
                    pthread_mutex_lock(&enrollment_log_lock);
                    fprintf(log_file, "%s NOENROLL %d\n", user->username, course_index);
                    fflush(log_file);
                    pthread_mutex_unlock(&enrollment_log_lock);
                    pthread_mutex_unlock(&courseArrayLocks[course_index]);
                    continue;
                }
                
                else{             
                    course_t * course = &courseArray[course_index];                                        //user can be enrolled
                    pthread_mutex_lock(&user->user_lock);
                    
                    //add user to course enrollment list (sorted by username)
                    InsertInOrder(&course->enrollment, (user));
                    printf("%s\n", ((user_t *)course->enrollment.head->data)->username);
                    
                    //update user's enrollment bitmask
                    user->courses.enrolled |= (1U << course_index);
                    uint32_t enrolled_mask = user->courses.enrolled;  // Store for logging
                    pthread_mutex_unlock(&user->user_lock);

                    //send success response
                    send_header->msg_type = OK;
                    send_header->msg_len = 0;
                    int ret = wr_msg(client_fd, send_header, (char*)NULL);
                    if(ret == -1){
                        printf("Error sending message to cleint\n");
                        pthread_mutex_unlock(&courseArrayLocks[course_index]);
                        break;
                    }
                    
                    //log successful enrollment
                    pthread_mutex_lock(&enrollment_log_lock);
                    fprintf(log_file, "%s ENROLL %d %u\n", user->username, course_index, enrolled_mask);
                    fflush(log_file);
                    pthread_mutex_unlock(&enrollment_log_lock);
                    pthread_mutex_unlock(&courseArrayLocks[course_index]);
                }

            }
        }
        //handle waitlist request
        if(header->msg_type == WAIT){
            //check if course_index is invalid
            bzero(in_buffer, BUFFER_SIZE);
            int received_size = read(client_fd, in_buffer, header->msg_len);
            if(received_size < 0){
                printf("Receiving failed\n");
                break;
            }

            int course_index = atoi(in_buffer);
            printf("%d\n", course_index);
            if(course_index < 0 || course_index > 31 || courseArray[course_index].title == NULL){       //invalid course 
                send_header->msg_type = ECNOTFOUND;
                send_header->msg_len = 0;
                int ret = wr_msg(client_fd, send_header, (char*)NULL);
                if(ret == -1){
                    printf("Error sending message to client\n");
                    break;
                }
                pthread_mutex_lock(&enrollment_log_lock);
                fprintf(log_file, "%s NOTFOUND_W %d\n", user->username, course_index);
                fflush(log_file);
                pthread_mutex_unlock(&enrollment_log_lock);
                continue;
            }
            else{ 
                //check if user is already enrolled or waitlisted
                pthread_mutex_lock(&user->user_lock);       
                if((user->courses.enrolled & (1U << course_index)) || (user->courses.waitlisted & (1U << course_index))){
                    pthread_mutex_unlock(&user->user_lock);
                    send_header->msg_type = ECDENIED;
                    send_header->msg_len = 0;
                    int ret = wr_msg(client_fd, send_header, (char*)NULL);
                    if(ret == -1){
                        printf("Error sending message to client\n");
                        break;
                    }
                    pthread_mutex_lock(&enrollment_log_lock);
                    fprintf(log_file, "%s NOWAIT %d\n", user->username, course_index);
                    fflush(log_file);
                    pthread_mutex_unlock(&enrollment_log_lock);
                    continue;
                }
                pthread_mutex_unlock(&user->user_lock);

                //check if course is not full (can't waitlist if space available)
                pthread_mutex_lock(&courseArrayLocks[course_index]);
                if(courseArray[course_index].maxCap > courseArray[course_index].enrollment.length){  //course not full
                    send_header->msg_type = ECDENIED;
                    send_header->msg_len = 0;
                    int ret = wr_msg(client_fd, send_header, (char*)NULL);
                    if(ret == -1){
                        printf("Error sending message to client\n");
                        pthread_mutex_unlock(&courseArrayLocks[course_index]);
                        break;
                    }
                    pthread_mutex_lock(&enrollment_log_lock);
                    fprintf(log_file, "%s NOWAIT %d\n", user->username, course_index);
                    fflush(log_file);
                    pthread_mutex_unlock(&enrollment_log_lock);
                    pthread_mutex_unlock(&courseArrayLocks[course_index]);
                    continue;
                }

                else{
                    course_t * course = &courseArray[course_index];                                        //user can be waitlisted
                    pthread_mutex_lock(&user->user_lock);

                    //add user to waitlist (FIFO order)
                    InsertAtTail(&course->waitlist, (user));

                    printf("%s\n", ((user_t *)course->waitlist.head->data)->username);
                    printf("gets alllllll the way here\n");
                    
                    //update user's waitlist bitmask
                    user->courses.waitlisted |= (1U << course_index);
                    uint32_t waitlisted_mask = user->courses.waitlisted;  // Store for logging
                    pthread_mutex_unlock(&user->user_lock);

                    send_header->msg_type = OK;
                    send_header->msg_len = 0;
                    int ret = wr_msg(client_fd, send_header, (char*)NULL);
                    if(ret == -1){
                        printf("Error sending message to cleint\n");
                        pthread_mutex_unlock(&courseArrayLocks[course_index]);
                        break;
                    }

                    //log successful waitlist addition
                    pthread_mutex_lock(&enrollment_log_lock);
                    fprintf(log_file, "%s WAIT %d %u\n", user->username, course_index, waitlisted_mask);
                    fflush(log_file);
                    pthread_mutex_unlock(&enrollment_log_lock);
                    pthread_mutex_unlock(&courseArrayLocks[course_index]);
                }
            }
        }
        //handle course drop request
        if(header->msg_type == DROP){
            //read course index from client
            bzero(in_buffer, BUFFER_SIZE);
            int received_size = read(client_fd, in_buffer, header->msg_len);
            if(received_size < 0){
                printf("Receiving failed\n");
                break;
            }

            int course_index = atoi(in_buffer);
            printf("%d\n", course_index);
            
            //validate course index and check if course exists
            if(course_index < 0 || course_index > 31 || courseArray[course_index].title == NULL){
                send_header->msg_type = ECNOTFOUND;
                send_header->msg_len = 0;
                int ret = wr_msg(client_fd, send_header, (char*)NULL);
                if(ret == -1){
                    printf("Error sending message to client\n");
                    break;
                }
                //log course not found for drop attempt
                pthread_mutex_lock(&enrollment_log_lock);
                fprintf(log_file, "%s NOTFOUND_D %d\n", user->username, course_index);
                fflush(log_file);
                pthread_mutex_unlock(&enrollment_log_lock);
                continue;
            }
            else{
                //check if user is already enrolled
                pthread_mutex_lock(&user->user_lock);       
                if(!(user->courses.enrolled & (1U << course_index))){
                    pthread_mutex_unlock(&user->user_lock);
                    send_header->msg_type = ECDENIED;
                    send_header->msg_len = 0;
                    int ret = wr_msg(client_fd, send_header, (char*)NULL);
                    if(ret == -1){
                        printf("Error sending message to client\n");
                        break;
                    }
                    //log denied drop attempt (user not enrolled)
                    pthread_mutex_lock(&enrollment_log_lock);
                    fprintf(log_file, "%s NODROP %d\n", user->username, course_index);
                    fflush(log_file);
                    pthread_mutex_unlock(&enrollment_log_lock);
                    continue;
                }
                else{
                    //user is enrolled so we can drop
                    pthread_mutex_lock(&courseArrayLocks[course_index]);
                    course_t * course = &courseArray[course_index];                                        

                    //remove user from course enrollment list
                    RemoveByValue(&course->enrollment, (user));
                    
                    //update user's enrollment bitmask (clear the bit)
                    user->courses.enrolled &= ~(1U << course_index);
                    uint32_t enrolled_mask = user->courses.enrolled;  // Store for logging
                    pthread_mutex_unlock(&user->user_lock);

                    //update server stats for successful drops
                    pthread_mutex_lock(&num_successful_drops_lock);
                    num_successful_drops++;
                    pthread_mutex_unlock(&num_successful_drops_lock);

                    //send success response
                    send_header->msg_type = OK;
                    send_header->msg_len = 0;
                    int ret = wr_msg(client_fd, send_header, (char*)NULL);
                    if(ret == -1){
                        printf("Error sending message to cleint\n");
                        pthread_mutex_unlock(&courseArrayLocks[course_index]);
                        break;
                    }

                    //log successful drop
                    pthread_mutex_lock(&enrollment_log_lock);
                    fprintf(log_file, "%s DROP %d %u\n", user->username, course_index, enrolled_mask);
                    fflush(log_file);
                    pthread_mutex_unlock(&enrollment_log_lock);
                    pthread_mutex_unlock(&courseArrayLocks[course_index]);
                }

            }
        }
    
    }
    
    //cleanup when client thread exits
    pthread_mutex_lock(&user->user_lock);
    user->socket_fd = -1;       //mark user as disconnected
    user->tid = 0;              //clear thread id
    pthread_mutex_unlock(&user->user_lock);
    free(header);
    free(send_header);
    close(client_fd);               
    printf("after logout\n");
    pthread_exit(NULL);
    return NULL;

}

//main server function
int main(int argc, char *argv[]) {
    //initialize user list with function pointers
    users = CreateList(comparatorFunc, printerFunc, deleterFunc);

    //init all mutex locks
    init_server_stats_locks(&num_accepted_connections_lock, &successful_client_logins_lock, &num_attempted_enrollments_lock, &num_successful_drops_lock, &enrollment_log_lock);

    init_course_array_locks(courseArrayLocks);
    
    //setup signal handler for graceful shutdown
    struct sigaction myaction = {{0}};
    myaction.sa_handler = sigint_handler;

    if(sigaction(SIGINT, &myaction, NULL) == -1){
        printf("signal handler failed to install\n");
    }

    //parse command line args
    int opt;
    while ((opt = getopt(argc, argv, "h")) != -1) {
        switch (opt) {
            case 'h':
                fprintf(stderr, USAGE_MSG);
                exit(EXIT_FAILURE);
        }
    }

    // 3 positional arguments necessary
    if (argc != 4) {
        fprintf(stderr, USAGE_MSG);
        exit(EXIT_FAILURE);
    }
    unsigned int port_number = atoi(argv[1]);
    char * poll_filename = argv[2];
    char * log_filename = argv[3];

    //validate port number
    if (port_number == 0){
        fprintf(stderr, "ERROR: Port number for server to listen is not given\n");
        fprintf(stderr, "Server Application Usage: %d -p <port_number>\n", port_number);
        exit(EXIT_FAILURE);
    }

    //load course data from file
    if(populate_courses(poll_filename, courseArray) != 1){      //populate courseArray
        exit(2);
    }
    
    //open log file 
    log_file = fopen(log_filename, "w");                 

    //initiate server and start listening on specified port
    int listen_fd = server_startup(port_number, poll_filename, log_filename); 
    
    //vars for client connection handling
    int client_fd = 0;
    struct sockaddr_in client_addr;
    unsigned int client_addr_len = sizeof(client_addr);
    pthread_t tid = 0;

    //main server loop - accept connections until SIGINT
    while(sigint_flag != 1){
        //check for shutdown signal
        if(sigint_flag == 1){
            break;
        }
        
        //block waiting for client connection
        client_fd = accept(listen_fd, (SA*)&client_addr, &client_addr_len);
        if(sigint_flag == 1){
            break;
        }
        if(client_fd < 0){
            if(sigint_flag == 1){
                break;
            }
            else{
                printf("server accept failed\n");
                exit(EXIT_FAILURE);
            }
        }
        else{
            //update connection stats
            pthread_mutex_lock(&num_accepted_connections_lock);
            num_accepted_connections++;
            pthread_mutex_unlock(&num_accepted_connections_lock);

            //read first message from client (must be LOGIN)
            petrV_header * header = calloc(1, sizeof(petrV_header));
            int ret = rd_msgheader(client_fd, header);
            if(ret == -1){
                printf("reading header failed\n");
                exit(EXIT_FAILURE);
            }
            if(header->msg_type != LOGIN){
                //terminate connection if not login
                close(client_fd);
            }
            else{
                //if msg is LOGIN 
                //check if user is in list
                bzero(buffer, BUFFER_SIZE);
                int received_size = read(client_fd, buffer, header->msg_len);
                if(received_size < 0){
                    printf("Receiving failed\n");
                    break;
                }
                        
                                //search for existing user in user list
                node_t * cur_node = users->head;
                int flag = 0;
                petrV_header * send_header = calloc(1, sizeof(petrV_header));
                
                while(cur_node){
                    user_t * user = (user_t*)cur_node->data;
                    
                    //check if this is the requested user
                    pthread_mutex_lock(&user->user_lock); 
                    
                    if(strcmp(user->username, buffer) == 0){
                        if(user->socket_fd >= 0){               //USER IN LIST AND ACTIVE (reject request)
                            send_header->msg_type = EUSRLGDIN;
                            send_header->msg_len = 0;
                            int ret = wr_msg(client_fd, send_header, NULL);
                            
                            //log rejected login attempt
                            pthread_mutex_lock(&enrollment_log_lock);
                            fprintf(log_file, "REJECT %s\n", user->username);
                            fflush(log_file);
                            pthread_mutex_unlock(&enrollment_log_lock);

                            pthread_mutex_unlock(&user->user_lock);
                            close(client_fd);
                            flag = 1;
                            if(ret == -1){
                                printf("failed to write to client\n");
                                exit(EXIT_FAILURE);
                            }
                            break;
                        }
                        else{                                   //USER IN LIST AND NOT ACTIVE
                            //create thread args and reconnect client 
                            args_t * thread_args = malloc(sizeof(args_t));
                            thread_args->client_ptr = client_fd;
                            thread_args->user_struct_ptr = user;
                            int pthread_ret = pthread_create(&user->tid, NULL, client_work, (void*)thread_args);
                            if(pthread_ret != 0){
                                printf("failed to create thread\n");
                                close(client_fd);
                                free(thread_args);
                                continue;
                            }
                            else{
                                pthread_mutex_lock(&successful_client_logins_lock);
                                successful_client_logins++;
                                pthread_mutex_unlock(&successful_client_logins_lock);
                            }

                            //update user status to active
                            user->socket_fd = 1;
                            

                            send_header->msg_type = OK;
                            send_header->msg_len = 0;
                            int ret = wr_msg(client_fd, send_header, NULL); 
                            
                            //log reconnection
                            pthread_mutex_lock(&enrollment_log_lock);
                            fprintf(log_file, "RECONNECTED %s\n", user->username);
                            fflush(log_file);
                            pthread_mutex_unlock(&enrollment_log_lock);

                            pthread_mutex_unlock(&user->user_lock);
                            flag = 1;
                            break;
                        }
                    }
                    pthread_mutex_unlock(&user->user_lock);
                    cur_node = cur_node->next;
                }
                if(flag == 0){                                  //USER NOT FOUND IN LIST
                    printf("inside user not foudn\n");
                    //create new user struct
                    user_t * new_user = malloc(sizeof(user_t));
                    char * new_username = malloc(header->msg_len);
                    strcpy(new_username, buffer);
                    
                    //init user locks and data
                    pthread_mutex_init(&new_user->user_lock, NULL);
                    pthread_mutex_init(&new_user->read_lock, NULL);
                    new_user->read_count = 0;
                    pthread_mutex_lock(&new_user->user_lock);

                    new_user->username = new_username;
                    new_user->socket_fd = 1;
                    new_user->tid = tid;

                    pthread_mutex_unlock(&new_user->user_lock);
                    
                    //init enrollment data to 0 (no courses)
                    new_user->courses.enrolled = 0;
                    new_user->courses.waitlisted = 0;

                    //add new user to sorted user list
                    InsertInOrder(users, new_user);
                    
                    send_header->msg_type = OK;
                    send_header->msg_len = 0;
                    
                    int ret = wr_msg(client_fd, send_header, (char*)NULL);
                    
                    //log new connection
                    pthread_mutex_lock(&enrollment_log_lock);
                    fprintf(log_file, "CONNECTED %s\n", new_user->username);
                    fflush(log_file);
                    pthread_mutex_unlock(&enrollment_log_lock);

                    //create thread for new user
                    args_t * thread_args = malloc(sizeof(args_t));
                    thread_args->client_ptr = client_fd;
                    thread_args->user_struct_ptr = new_user; 
                    int pthread_ret = pthread_create(&new_user->tid, NULL, client_work, (void*)thread_args);
                    if(pthread_ret != 0){
                        printf("failed to create thread\n");
                        close(client_fd);
                        //free(client_fd);
                        free(thread_args);
                        continue;
                    }
                    else{
                        pthread_mutex_lock(&successful_client_logins_lock);
                        successful_client_logins++;
                        pthread_mutex_unlock(&successful_client_logins_lock);
                    }
                }
            free(send_header);
            free(header);
            //free(thread_args);
            }
        }
    }

    //SIGINT received - start shutdown
    close(listen_fd);
    //send kill signal to all client threads
    node_t * ptr = users->head;
    while(ptr){
        user_t * user = (user_t*)ptr->data;
        pthread_mutex_lock(&user->user_lock);
        if(user->socket_fd != -1){
            pthread_t tid = user->tid;  // Store tid before unlocking (unlock here to let client thread do work)
            pthread_mutex_unlock(&user->user_lock);
            pthread_kill(tid, SIGINT);
            printf("killed thread\n");
            pthread_join(tid, NULL);
        }
        else{
            pthread_mutex_unlock(&user->user_lock);
        }
        ptr = ptr->next;
    }
    
    printf("successfully killed all client threads\n");
    //output stats
    int index = 0;
    //print course statistics: title, max cap, current enrollment, enrolled users, waitlisted users
    while(courseArray[index].title != NULL){        //might need to write helper funcs to get formatting right on enroll/waitlist
        char enrollment_buf[BUFFER_SIZE] = {'\0'};       //initialize
        char waitlist_buf[BUFFER_SIZE] = {'\0'};

        //build semicolon-separated user lists
        print_linked_list(&courseArray[index].enrollment, NULL, enrollment_buf);
        print_linked_list(&courseArray[index].waitlist, NULL, waitlist_buf);

        fprintf(stdout, "%s, %d, %d, %s, %s\n", courseArray[index].title, courseArray[index].maxCap, courseArray[index].enrollment.length, enrollment_buf, waitlist_buf);
        index++;
    }
    
    printf("after first while loop\n");
    //print user statistics: username, enrolled bitmask, waitlisted bitmask
    node_t * user_ptr = users->head;
     
    while(user_ptr){
        user_t * user = (user_t *)user_ptr->data;
        fprintf(stderr, "%s, %d, %d\n", user->username, user->courses.enrolled, user->courses.waitlisted);
        user_ptr = user_ptr->next;
    }

    //print server statistics: connections, logins, enrollments, drops
    fprintf(stderr, "%d, %d, %d, %d\n", num_accepted_connections, successful_client_logins, num_attempted_enrollments, num_successful_drops); 
   
    //cleanup user list and allocated memory
    node_t * cleanup_ptr = users->head;
    while(cleanup_ptr){
        user_t * user = (user_t*)cleanup_ptr->data;
        node_t * next = cleanup_ptr->next;
        free(user->username);
        free(user);
        free(cleanup_ptr);
        cleanup_ptr = next;
    }
    users->head = NULL;

    //WRITE A FUNC TO CLEAN UP USERS LIST 
    fclose(log_file);
    return 0;

}