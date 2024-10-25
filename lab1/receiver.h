#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <semaphore.h>
#include <time.h>
#include <sys/mman.h>
#include <mqueue.h>

#define MAX_MESSAGE_SIZE 1025

typedef struct {
    int flag;      // 1 for message passing, 2 for shared memory
    union{
        //int msqid; //for system V api. You can replace it with struecture for POSIX api
        mqd_t mqd;         // POSIX message queue descriptor
        char* shm_addr;
    }storage;
    sem_t* sem_send;
    sem_t* sem_receive;
} mailbox_t;


typedef struct {
    /*  TODO: 
        Message structure for wrapper
    */
    char msg_text[MAX_MESSAGE_SIZE];  // Message text
} message_t;

void receive(message_t* message_ptr, mailbox_t* mailbox_ptr);
