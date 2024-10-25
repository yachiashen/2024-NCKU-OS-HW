#include "receiver.h"

double time_taken = 0.0;

void receive(message_t* message_ptr, mailbox_t* mailbox_ptr){
    /*  TODO: 
        1. Use flag to determine the communication method
        2. According to the communication method, receive the message
    */
    struct timespec start, end;
    
    sem_post(mailbox_ptr->sem_receive);
    sem_wait(mailbox_ptr->sem_send);
    
    if (mailbox_ptr->flag == 1) {
        // Message passing using POSIX message queue
        clock_gettime(CLOCK_MONOTONIC, &start);
        mq_receive(mailbox_ptr->storage.mqd, message_ptr->msg_text, MAX_MESSAGE_SIZE, NULL);
        clock_gettime(CLOCK_MONOTONIC, &end);
        time_taken += (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) * 1e-9;
        
    } else if (mailbox_ptr->flag == 2) {
        clock_gettime(CLOCK_MONOTONIC, &start);
        //strncpy(message_ptr->msg_text, mailbox_ptr->storage.shm_addr, MAX_MESSAGE_SIZE);
        strcpy(message_ptr->msg_text, mailbox_ptr->storage.shm_addr);
        clock_gettime(CLOCK_MONOTONIC, &end);
        time_taken += (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) * 1e-9;
        
    }
}

int main(int argc, char *argv[]){
    /*  TODO: 
        1) Call receive(&message, &mailbox) according to the flow in slide 4
        2) Measure the total receiving time
        3) Get the mechanism from command line arguments
            • e.g. ./receiver 1
        4) Print information on the console according to the output format
        5) If the exit message is received, print the total receiving time and terminate the receiver.c
    */
    if (argc != 2) {
        printf("Usage: ./receiver <mechanism>\n");
        return -1;
    }

    int mechanism = atoi(argv[1]);

    // Initialize mailbox
    mailbox_t mailbox;
    mailbox.flag = mechanism;

    if (mechanism == 1) {
        // POSIX message queue setup
        mailbox.storage.mqd = mq_open("/msg_queue", O_CREAT | O_RDONLY, 0666, NULL);
        mailbox.sem_send = sem_open("sender", 0);
        mailbox.sem_receive = sem_open("receiver", 0);
        if (mailbox.storage.mqd == (mqd_t)-1) {
            perror("mq_open");
            return -1;
        }
        if (mailbox.sem_send == SEM_FAILED || mailbox.sem_receive == SEM_FAILED) {
            perror("sem_open");
            return -1;
        }
        printf("Message Passing\n");
    } else if (mechanism == 2) {
        // POSIX shared memory setup
        int shm_fd = shm_open("/shm_memory", O_CREAT | O_RDONLY, 0666);
        mailbox.sem_send = sem_open("sender", 0);
        mailbox.sem_receive = sem_open("receiver", 0);
        if (shm_fd == -1) {
            perror("shm_open");
            return -1;
        }
        if (mailbox.sem_send == SEM_FAILED || mailbox.sem_receive == SEM_FAILED) {
            perror("sem_open");
            return -1;
        }
        // Map shared memory
        mailbox.storage.shm_addr = mmap(NULL, MAX_MESSAGE_SIZE, PROT_READ, MAP_SHARED, shm_fd, 0);
        if (mailbox.storage.shm_addr == MAP_FAILED) {
            perror("mmap");
            return -1;
        }
        
        
        printf("Share Memory\n");
    }
    
    
    message_t message;
    while (1) {
        // Receive message
        receive(&message, &mailbox);
        if (strcmp(message.msg_text, "exit") == 0) {
            break;
        }
            
        printf("Receiving message: %s\n", message.msg_text);
    }
    printf("Sender exit!\n");
    printf("Total time taken in receiving msg: %.6fs\n", time_taken);

    // Cleanup
    if (mechanism == 1) {
        mq_close(mailbox.storage.mqd);
        mq_unlink("/msg_queue");
        sem_close(mailbox.sem_send); 
        sem_close(mailbox.sem_receive);
        sem_unlink("/sender");
        sem_unlink("/receiver");
        
    } else if (mechanism == 2) {
        munmap(mailbox.storage.shm_addr, MAX_MESSAGE_SIZE);
        shm_unlink("/shm_memory");
        sem_close(mailbox.sem_send);
        sem_close(mailbox.sem_receive);
        sem_unlink("/sender");
        sem_unlink("/receiver");
    } else {
        perror("argv[1]");
        return -1;
    }

    return 0;
}
