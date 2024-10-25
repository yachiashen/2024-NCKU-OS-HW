#include "sender.h"

double time_taken = 0.0;


void send(message_t message, mailbox_t* mailbox_ptr){
    /*  TODO: 
        1. Use flag to determine the communication method
        2. According to the communication method, send the message
    */
    struct timespec start, end;
    sem_wait(mailbox_ptr->sem_receive);

    if (mailbox_ptr->flag == 1) {
        // Message passing using POSIX message queue
        clock_gettime(CLOCK_MONOTONIC, &start);  // Start time
        if (mq_send(mailbox_ptr->storage.mqd, message.msg_text, MAX_MESSAGE_SIZE, 0) == -1) {
            perror("mq_send");
        }
        clock_gettime(CLOCK_MONOTONIC, &end);    // End time
    } else if (mailbox_ptr->flag == 2) {
        clock_gettime(CLOCK_MONOTONIC, &start);  // Start time
        //strncpy(mailbox_ptr->storage.shm_addr, message.msg_text, MAX_MESSAGE_SIZE - 1);
        strcpy(mailbox_ptr->storage.shm_addr, message.msg_text);
        mailbox_ptr->storage.shm_addr[MAX_MESSAGE_SIZE - 1] = '\0';  // Ensure null-termination
        clock_gettime(CLOCK_MONOTONIC, &end);    // End time
    }


    time_taken += (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) * 1e-9;
    sem_post(mailbox_ptr->sem_send);


    //printf("Message sent in %f seconds\n", time_taken);
}

int main(int argc, char *argv[]){
    /*  TODO: 
        1) Call send(message, &mailbox) according to the flow in slide 4
        2) Measure the total sending time
        3) Get the mechanism and the input file from command line arguments
            • e.g. ./sender 1 input.txt
                    (1 for Message Passing, 2 for Shared Memory)
        4) Get the messages to be sent from the input file
        5) Print information on the console according to the output format
        6) If the message form the input file is EOF, send an exit message to the receiver.c
        7) Print the total sending time and terminate the sender.c
    */
    
    if (argc != 3) {
        printf("Usage: ./sender <mechanism> <input_file>\n");
        return -1;
    }

    int mechanism = atoi(argv[1]);
    char *input_file = argv[2];

    // Initialize mailbox
    mailbox_t mailbox;
    mailbox.flag = mechanism;

    if (mechanism == 1) {
        // POSIX message queue setup
        struct mq_attr attr = {0, 10, MAX_MESSAGE_SIZE*sizeof(char), 0};
        
        mailbox.storage.mqd = mq_open("/msg_queue", O_CREAT | O_WRONLY, 0666, &attr);
        mailbox.sem_send = sem_open("/sender", O_CREAT, 0666, 0);
        mailbox.sem_receive = sem_open("/receiver", O_CREAT, 0666, 0);
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
        int shm_fd = shm_open("/shm_memory", O_CREAT | O_RDWR, 0666);
        mailbox.sem_send = sem_open("/sender", O_CREAT, 0666, 0);
        mailbox.sem_receive = sem_open("/receiver", O_CREAT, 0666, 0);
        if (shm_fd == -1) {
            perror("shm_open");
            return -1;
        }
        if (mailbox.sem_send == SEM_FAILED || mailbox.sem_receive == SEM_FAILED) {
            perror("sem_open");
            return -1;
        }
        
        if(ftruncate(shm_fd, MAX_MESSAGE_SIZE) == -1){
          perror("shm_ftruncate");
          return -1;
        }
        
        mailbox.storage.shm_addr = mmap(NULL, MAX_MESSAGE_SIZE, PROT_WRITE, MAP_SHARED, shm_fd, 0);
        if (mailbox.storage.shm_addr == MAP_FAILED) {
            perror("mmap");
            return -1;
        }
        printf("Share Memory\n");
    }

    // Open the input file
    FILE *file = fopen(input_file, "r");
    if (!file) {
        perror("fopen");
        return -1;
    }

    // Read messages from the input file and send
    char line[MAX_MESSAGE_SIZE];
    message_t message;

    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = '\0';  // Remove newline character
        strncpy(message.msg_text, line, MAX_MESSAGE_SIZE);
        printf("Sending message: %s\n", message.msg_text);
        send(message, &mailbox);
    }

    // Send exit message
    strncpy(message.msg_text, "exit", MAX_MESSAGE_SIZE);
    printf("End of input file! exit!\n");
    send(message, &mailbox);
    printf("Total time taken in sending msg: %.6fs\n", time_taken);

    // Cleanup
    fclose(file);
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
