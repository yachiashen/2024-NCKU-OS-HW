#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "../include/command.h"
#include "../include/builtin.h"

// ======================= requirement 2.3 =======================
/**
 * @brief 
 * Redirect command's stdin and stdout to the specified file descriptor
 * If you want to implement ( < , > ), use "in_file" and "out_file" included the cmd_node structure
 * If you want to implement ( | ), use "in" and "out" included the cmd_node structure.
 *
 * @param p cmd_node structure
 * 
 */
void redirection(struct cmd_node *p){

    // Handle input redirection ( < )
    if (p->in_file != NULL) {
        int in_fd = open(p->in_file, O_RDONLY);
        if (in_fd == -1) {
            perror("open input file");
            exit(EXIT_FAILURE);
        }
        if (dup2(in_fd, STDIN_FILENO) == -1) {  // Redirect stdin to the input file
            perror("dup2 input file");
            exit(EXIT_FAILURE);
        }
        close(in_fd);  // Close the file descriptor after duplication
    }

    // Handle output redirection ( > )
    if (p->out_file != NULL) {
        // Check if file already exists
        if (access(p->out_file, F_OK) == 0) {
            printf("Warning: output file '%s' already exists. Overwrite? (y/n): ", p->out_file);
            char response;
            if (scanf(" %c", &response) != 1 || (response != 'y' && response != 'Y')) {
                printf("Aborted redirection.\n");
                return;
            }
        }
        int out_fd = open(p->out_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out_fd == -1) {
            perror("open output file");
            exit(EXIT_FAILURE);
        }
        if (dup2(out_fd, STDOUT_FILENO) == -1) {  // Redirect stdout to the output file
            perror("dup2 output file");
            exit(EXIT_FAILURE);
        }
        close(out_fd);  // Close the file descriptor after duplication
    }
	
}
// ===============================================================

// ======================= requirement 2.2 =======================
/**
 * @brief 
 * Execute external command
 * The external command is mainly divided into the following two steps:
 * 1. Call "fork()" to create child process
 * 2. Call "execvp()" to execute the corresponding executable file
 * @param p cmd_node structure
 * @return int 
 * Return execution status
 */
int spawn_proc(struct cmd_node *p)
{
    pid_t pid = fork();  // Step 1: Create a child process

    if (pid == -1) {
        perror("fork");  // Handle fork error
        return -1;
    }
    if (pid == 0) {
        // Child process
        
        redirection(p);      // Call redirection to handle < and > operators
        
        if (execvp(p->args[0], p->args) == -1) {  // Step 2: Execute the command
            perror("execvp");
            exit(EXIT_FAILURE);  // Exit if execvp fails
        }
    } else {
        // Parent process
        int status;
        if (waitpid(pid, &status, 0) == -1) {  // Wait for child to complete
            perror("waitpid");
            return -1;
        }
        if(WIFEXITED(status)){
          return 1;
        }else{
          return -1;
        }
        //return WIFEXITED(status) ? WEXITSTATUS(status) : -1;  // Return child's exit status
    }

    return 1;
}
// ===============================================================


// ======================= requirement 2.4 =======================
/**
 * @brief 
 * Use "pipe()" to create a communication bridge between processes
 * Call "spawn_proc()" in order according to the number of cmd_node
 * @param cmd Command structure  
 * @return int
 * Return execution status 
 */
int fork_cmd_node(struct cmd *cmd)
{
    int pipe_fd[2];
    int in_fd = STDIN_FILENO;  // Initially, stdin for the first command
    struct cmd_node *current = cmd->head;

    while (current != NULL) {
        // If there is a next command, create a pipe
        if (current->next != NULL) {
            if (pipe(pipe_fd) == -1) {
                perror("pipe");
                exit(EXIT_FAILURE);
            }
        }

        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            exit(EXIT_FAILURE);
        }
        
        if (pid == 0) {  // Child process
            // Set up input redirection for the current command
            if (in_fd != STDIN_FILENO) {
                if (dup2(in_fd, STDIN_FILENO) == -1) {
                    perror("dup2 input");
                    exit(EXIT_FAILURE);
                }
                close(in_fd);
            }

            // Set up output redirection for the current command
            if (current->next != NULL) {
                if (dup2(pipe_fd[1], STDOUT_FILENO) == -1) {
                    perror("dup2 output");
                    exit(EXIT_FAILURE);
                }
                close(pipe_fd[1]);
            }

            // Close unused pipe ends in the child process
            close(pipe_fd[0]);

            // Execute command with redirection handled
            redirection(current);
            if (execvp(current->args[0], current->args) == -1) {
                perror("execvp");
                exit(EXIT_FAILURE);
            }
        } else {  // Parent process
            // Close pipes and manage flow control
            close(pipe_fd[1]);  // Close the write end of the current pipe
            if (in_fd != STDIN_FILENO) {
                close(in_fd);  // Close the old read end
            }
            in_fd = pipe_fd[0];  // Save the read end for the next command

            // Move to the next command in the pipeline
            current = current->next;
        }
    }

    // Parent: wait for all child processes to complete
    while (wait(NULL) > 0) {
        // Wait for each child process to finish
    }
    return 1;
}
// ===============================================================


void shell()
{
	while (1) {
		printf(">>> $ ");
		char *buffer = read_line();
		if (buffer == NULL)
			continue;

		struct cmd *cmd = split_line(buffer);
		
		int status = -1;
		// only a single command
		struct cmd_node *temp = cmd->head;
		
		if(temp->next == NULL){
			status = searchBuiltInCommand(temp);
			if (status != -1){
				int in = dup(STDIN_FILENO), out = dup(STDOUT_FILENO);
				if( in == -1 | out == -1)
					perror("dup");
				redirection(temp);
				status = execBuiltInCommand(status,temp);

				// recover shell stdin and stdout
				if (temp->in_file)  dup2(in, 0);
				if (temp->out_file){
					dup2(out, 1);
				}
				close(in);
				close(out);
			}
			else{
				//external command
				status = spawn_proc(cmd->head);
			}
		}
		// There are multiple commands ( | )
		else{
			
			status = fork_cmd_node(cmd);
		}
		// free space
		while (cmd->head) {
			
			struct cmd_node *temp = cmd->head;
      		cmd->head = cmd->head->next;
			free(temp->args);
   	    	free(temp);
   		}
		free(cmd);
		free(buffer);
		
		if (status == 0)
			break;
	}
}
