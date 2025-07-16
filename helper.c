#include "helper.h"
#include "server.h"
#include "dlinkedlist.h"
#include "protocol.h"
#include "debug.h"

int total_courses = 0;
volatile sig_atomic_t sigint_flag = 0;

int server_startup(int port_number, char * course_filename, char * log_filename){
    int sockfd;
    struct sockaddr_in servaddr;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1){
//        printf("socket creation failed\n");   //REPLACE
        exit(EXIT_FAILURE);
    }
    else{
  //      printf("Socket successfully created");
    }

    bzero(&servaddr, sizeof(servaddr));

    //assign IP, PORT
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port_number);

    int opt=1;
    if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, (char *)&opt, sizeof(opt))<0){
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
    
    //Binding newly created socket to given IP and verif
    if ((bind(sockfd, (SA*)&servaddr, sizeof(servaddr))) != 0){
        printf("socket bind failed\n");                             //REPLACE IF NEEDED
        exit(EXIT_FAILURE);
    }
    //else{
    //    printf("Socket successfully binded\n");
    //}
    
    //Now server is ready to listen and verif
    if ((listen(sockfd, 1)) != 0){
        printf("Listen failed\n");
        exit(EXIT_FAILURE);
    }
    else{
        printf("Server initialized with %d courses.\n", total_courses);
        printf("Currently listening on port %d.\n", port_number);
    }

    return sockfd;

}


int comparatorFunc(const void * a, const void * b){
    if(a == NULL && b == NULL){return 0;}
    if(a == NULL){return -1;}
    if(b == NULL){return -1;}
    
    user_t * A = (user_t*)a;
    user_t * B = (user_t*)b;
    return strcmp(A->username, B->username);
}

void printerFunc(void * a, void * b){
    if(a==NULL || b==NULL){return;}
    FILE * fp = (FILE*)b;

    user_t * A = (user_t*)a;
    if(A->username == NULL){return;}
    fprintf(fp, "username : %s, socket: %d, pthread id: %p\n, enrollment:\ncourses: %u\n, waitlisted: %u\n",
                A->username, A->socket_fd, (void*)A->tid, A->courses.enrolled, A->courses.waitlisted);
}

void deleterFunc(void * a){
    //deletes node
    if(a == NULL){return;}

    user_t * cur_node = (user_t*)a;
    free(cur_node->username);
    //free(cur_node->courses);      //CHECK if these are dynamically allocated
    free(cur_node);
    
    cur_node = NULL;
    return;
}

void rm_and_delete_node(dlist_t* users, void *a){
    if(a==NULL || users == NULL){return;}

    node_t * cur_node = (node_t*)a;

    if(cur_node->prev != NULL){
        cur_node->prev->next = cur_node->next;
    }
    else{
        users->head = cur_node->next;
    }
    if(cur_node->next != NULL){
        cur_node->next->prev = cur_node->prev;
    }
  
    printf("before all frees\n");
    user_t * userA = (user_t*)cur_node->data;
    free(userA->username);
    printf("frees username\n");
    //free(cur_node->courses);
    free(userA);
    printf("frees user struct\n");
    free(cur_node);
    printf("frees node\n");
    
    cur_node = NULL;
    userA = NULL;
    users->length--;
    return;

}

int populate_courses(char * course_file, course_t * courseArray){
    FILE * file = fopen(course_file, "r");
    if(file==NULL){
        printf("file open failed");
        return -1;
    }
    int i = 0;
    char * line = NULL;
    size_t size = 0;

    while(getline(&line, &size, file) != -1){
        char * course_title = strtok(line, ";");
        int max_cap = atoi(strtok(NULL, ";"));

        char * malloced_course_title = malloc(strlen(course_title)+1);
        strcpy(malloced_course_title, course_title);

        courseArray[i].title = malloced_course_title;
        courseArray[i].maxCap = max_cap;
        //courseArray[i].enrollment = &CreateList(comparatorFunc, printerFunc, deleterFunc);
        //courseArray[i].waitlist = &CreateList(comparatorFunc, printerFunc, deleterFunc);
        courseArray[i].enrollment.head = NULL;
        courseArray[i].enrollment.length = 0;
        courseArray[i].enrollment.comparator = comparatorFunc;
        courseArray[i].enrollment.printer = printerFunc;
        courseArray[i].enrollment.deleter = deleterFunc;
        courseArray[i].waitlist.head = NULL;
        courseArray[i].waitlist.length = 0;
        courseArray[i].waitlist.comparator = comparatorFunc;
        courseArray[i].waitlist.printer = printerFunc;
        courseArray[i].waitlist.deleter = deleterFunc;

        i++;
        total_courses++;
    }

    while(i<32){
        courseArray[i].title= NULL;
        i++;
    }
    free(line);
    fclose(file);

    return 1;
}


void sigint_handler(int sig){
    sigint_flag = 1;
}

//MAKE A CLEAN UP FUNC FOR ALL ALLOCATED MEM ON EXIT

void init_server_stats_locks(pthread_mutex_t * num_accepted_connections_lock, pthread_mutex_t * successful_client_logins_lock, pthread_mutex_t * num_attempted_enrollments_lock, pthread_mutex_t * num_successful_drops_lock, pthread_mutex_t * enrollment_log_lock){
    pthread_mutex_init(num_accepted_connections_lock, NULL);

    pthread_mutex_init(successful_client_logins_lock, NULL);

    pthread_mutex_init(num_attempted_enrollments_lock, NULL);

    pthread_mutex_init(num_successful_drops_lock, NULL);

    pthread_mutex_init(enrollment_log_lock, NULL);
}


void init_course_array_locks(pthread_mutex_t * locks_array){
    int i = 0;
    while(i<32){
        pthread_mutex_init(&locks_array[i], NULL);
        i++;
    }
}

void init_user_locks(dlist_t * user_LL){
    node_t * cur_node = user_LL->head;
    while(cur_node){
        user_t * user = (user_t *)cur_node->data;
        pthread_mutex_init(&user->user_lock, NULL);
        pthread_mutex_init(&user->read_lock, NULL);
        cur_node = cur_node->next;
    }
}
/*
void print_user_linked_list(dlist_t * user_LL, FILE * fp, char * buf){
    if(user_LL == NULL){return;}

    node_t* head = user_LL->head;
    while (head != NULL) {
        user_t * user = (user_t*)head->data;
        if(user != NULL && user->username != NULL){
            strncat(buf, user->username, BUFFER_SIZE - strlen(buf) -1);
            if(head->next != NULL){
                strncat(buf, ";", BUFFER_SIZE - strlen(buf) - 1);
            }
        }
        head = head->next;
    }
    return; 
}
*/
void print_linked_list(dlist_t * user_LL, FILE * fp, char * buf){
    if(user_LL == NULL){return;}
    buf[0] = '\0';
    int buf_length = 0;

    node_t* head = user_LL->head;
    while (head != NULL) {
        user_t * user = (user_t*)head->data;
        if(user != NULL && user->username != NULL){
            //strncat(buf, user->username, BUFFER_SIZE - strlen(buf) -1);
            buf_length += snprintf(buf + buf_length, BUFFER_SIZE - buf_length, "%s", user->username);
            if(head->next != NULL){
                //strncat(buf, ";", BUFFER_SIZE - strlen(buf) - 1);
                buf_length += snprintf(buf + buf_length, BUFFER_SIZE - buf_length, ";");
            }
        }
        head = head->next;
    }
    return;
}
/*
void print_EW_linked_list(dlist_t * LL, FILE * fp, char * buf){
    if(LL == NULL){return;}

    buf[0] = '\0';
    int buf_length = 0;
    node_t* head = LL->head;
    while (head != NULL) {
        user_t * user = (user_t*)head->data;        //data is a ptr to a user
        if(user != NULL && user->username != NULL){
            //strncat(buf, user->username, BUFFER_SIZE - strlen(buf) -1);
            buf_length += snprintf(buf + buf_length, BUFFER_SIZE - buf_length, "%s", user->username);
            if(head->next != NULL){
                buf_length += snprintf(buf + buf_length, BUFFER_SIZE - buf_length, ";");
            }
        }
        head = head->next;
    }
    return; 
}

*/
