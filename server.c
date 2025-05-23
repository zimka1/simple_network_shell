/* ==============================================================================================
 * Server Module
 * ==============================================================================================
 *
 * This module implements the core server-side logic of the interactive shell application.
 * The server supports two types of socket interfaces:
 *   - UNIX domain sockets (local IPC via file path)
 *   - TCP/IP sockets (remote clients via host/port)
 *
 * The server listens for incoming connections and forks a child process for each client.
 * Clients send shell-like commands which may include piping, redirection, or special
 * internal commands such as `halt`, `quit`, `stat`, and `abort`. The parent process
 * coordinates child sessions, processes control commands through a pipe, and maintains
 * metadata about active connections.
 *
 * The module also includes:
 *   - Parsing of shell commands, separating arguments, pipes, redirection, etc.
 *   - Command execution with fork/exec and inter-process pipes
 *   - I/O redirection using custom handlers (redirections.h)
 *   - Linked list management for tracking connections
 *   - Bidirectional communication with clients using sockets
 *
 * ==============================================================================================
 */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include "redirections.h"

#define CHUNK_SIZE 500


/* ==============================================================================================
 * Filename Parser for Redirection
 * ==============================================================================================
 * Parses filenames from the input command stream after encountering redirection operators:
 *   - '>'   → output redirection
 *   - '>>'  → output append redirection
 *   - '<'   → input redirection
 * Skips whitespaces and reads characters until a special shell delimiter is reached
 * (such as pipe '|', semicolon ';', newline, or null terminator).
 * Returns a newly allocated string containing the parsed filename.
 * ==============================================================================================
 */


char* read_filename(char **cur_char){
    char *filename = (char *)calloc(20, sizeof(char));

    char *cur_char_filename = filename;

    while (**cur_char != ';' && **cur_char != '|' && **cur_char != '\n' && **cur_char != '\0') {
        if (**cur_char == ' ') {
            (*cur_char)++;
            continue;
        }
        *(cur_char_filename++) = **cur_char;
        (*cur_char)++;
    }
    return filename;
}


/* ==============================================================================================
 * Free Command Argument Memory
 * ==============================================================================================
 * Frees dynamically allocated memory for command arguments
 * used during parsing and execution.
 * ==============================================================================================
 */


void free_args(char ****args, int num_commands, int num_args_per_command) {
    for (int row = 0; row < num_commands; row++) {
        for (int call = 0; call < num_args_per_command; call++) {
            free((*args)[row][call]);
        }
        free((*args)[row]);
    }
    free((*args));
    *args = NULL;
}


/* ==============================================================================================
 * Execute Parsed Commands (Pipeline Execution)
 * ==============================================================================================
 * Executes a sequence of parsed commands that form a pipeline.
 * Each command is forked as a separate process. Pipes are created
 * between them to connect stdout of one to stdin of the next.
 *
 * Handles redirection:
 *   - Input redirection if '<' is used before a command
 *   - Output redirection with '>' or '>>' at the end of a pipeline
 *
 * If the last process produces output, it is captured via a dedicated result pipe
 * and sent back to the client over the socket, or printed locally if no socket is set.
 * Built-in commands such as 'cd' and 'halt' are also handled here without forking.
 * ==============================================================================================
 */


void execute_command(int client_fd, char ***args, char **filenames, int row_number,
                     const int *input_file_flags, const int *output_file_flags, int not_all_flag) {

    char chunk_buf[4096];
    memset(chunk_buf, 0, sizeof(chunk_buf));
    int total = 0, bytes_read;
    char info_message[256] = "";

    // Handle built-in 'halt' command: terminates the entire shell server
    if (strcmp(args[0][0], "halt") == 0) {
        if (client_fd > 0) {
            write(client_fd, "[HALT]", 6);
            write(client_fd, "[END]", 5);
        }
        printf("Server closed.\n");
        killpg(0, SIGTERM); // Send termination to all processes in group
    }

    // Handle built-in 'help' command: prints available internal commands
    if (strcmp(args[0][0], "help") == 0) {
        snprintf(info_message, sizeof(info_message),
                 "[HELP] Available internal commands:\n"
                 "  help           Show this message\n"
                 "  cd <path>      Change working directory\n"
                 "  halt           Shut down the entire server\n"
                 "  quit           Disconnect current client\n"
                 "  stat           Show active connections (server only)\n"
                 "  abort <id>     Force-close a specific connection by ID\n");

        // Send to client or print to stdout
        if (client_fd > 0) {
            write(client_fd, info_message, strlen(info_message));
            write(client_fd, "[END]", 5);
        } else {
            printf("%s\n", info_message);
        }
        return;
    }

    // Handle built-in 'cd' command: changes working directory
    if (strcmp(args[0][0], "cd") == 0) {
        if (args[0][1] == NULL) {
            snprintf(info_message, sizeof(info_message), "[ERROR] cd: missing argument\n");
        } else if (chdir(args[0][1]) != 0) {
            snprintf(info_message, sizeof(info_message), "[ERROR] cd: directory doesn't exist\n");
        } else {
            snprintf(info_message, sizeof(info_message), "[INFO] Changed directory to: %s\n", args[0][1]);
        }
        if (info_message[0] != '\0') {
            strncat(chunk_buf, info_message, sizeof(chunk_buf) - strlen(chunk_buf) - 1);
        }
        total = strlen(chunk_buf);
        chunk_buf[total] = '\0';

        // Send feedback to client or print to local terminal
        if (client_fd > 0) {
            write(client_fd, chunk_buf, total);
            write(client_fd, "[END]", 5);
        } else printf("%s\n", chunk_buf);
        return;
    }

    // Declare pipes between commands in pipeline
    int pipes[row_number][2];
    pid_t pids[row_number + 1];
    int result_pipe[2]; // Pipe used by the last command to send output to parent

    // Create result pipe
    if (pipe(result_pipe) == -1) {
        perror("[ERROR] result_pipe error");
        exit(1);
    }

    // Create all intermediate pipes for command chaining
    for (int i = 0; i < row_number; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("[ERROR] Pipe error");
            exit(1);
        }
    }

    // Fork each command in the pipeline
    for (int i = 0; i <= row_number; i++) {
        pids[i] = fork();
        if (pids[i] == 0) {
            // CHILD PROCESS

            // Handle input redirection or pipe from previous command
            if (input_file_flags[i]) {
                input_redirection(filenames[i]);
            } else if (i > 0) {
                dup2(pipes[i - 1][0], STDIN_FILENO);
            }

            // Handle output redirection or pipe to next command or result pipe
            if (output_file_flags[i] == 1) {
                output_redirection(filenames[i]);
            } else if (output_file_flags[i] == 2) {
                output_redirection_append(filenames[i]);
            } else if (i < row_number) {
                dup2(pipes[i][1], STDOUT_FILENO);
            } else {
                dup2(result_pipe[1], STDOUT_FILENO); // last command sends result here
            }

            // Redirect STDERR to STDOUT for error capturing
            dup2(STDOUT_FILENO, STDERR_FILENO);

            // Close all pipes not used by this child
            for (int j = 0; j < row_number; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            // Close unused ends of result pipe
            close(result_pipe[0]);
            if (i != row_number || output_file_flags[i]) {
                close(result_pipe[1]);
            }

            // Execute the command using execvp
            execvp(args[i][0], args[i]);
            perror("[ERROR] Execution error");
            exit(1);
        } else if (pids[i] < 0) {
            perror("[ERROR] Fork error");
            exit(1);
        }
    }

    // PARENT PROCESS

    // Close write-end of result pipe (only reading)
    close(result_pipe[1]);

    // Close all intermediate pipes in parent
    for (int i = 0; i < row_number; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    // Read the output from result pipe and forward to client or print
    while ((bytes_read = read(result_pipe[0], chunk_buf, sizeof(chunk_buf))) > 0) {
        fwrite(chunk_buf, 1, bytes_read, stdout);
        if (client_fd > 0)
            write(client_fd, chunk_buf, bytes_read);
    }

    // Wait for all forked child processes to finish
    for (int i = 0; i <= row_number; i++) {
        waitpid(pids[i], NULL, 0);
    }

    // If no output was captured and output was redirected to a file, inform the client
    if (!bytes_read) {
        int filename_index = -1;
        for (int i = 0; i <= row_number; i++) {
            if (output_file_flags[i]) {
                filename_index = i;
                break;
            }
        }
        if (filename_index != -1) {
            snprintf(info_message, sizeof(info_message), "[INFO] Output saved to file: %s\n", filenames[filename_index]);
            write(client_fd, info_message, strlen(info_message));
        }
    }

    // Send [END] tag to indicate response finished (unless suppressed by not_all_flag)
    if (!not_all_flag)
        write(client_fd, "[END]", 7);

    // Close result pipe's read-end
    close(result_pipe[0]);
}



/* ==============================================================================================
 * Command Parsing and Handling
 * ==============================================================================================
 * Parses a raw command string received from the client or entered in script/server mode.
 * Supports multiple commands separated by semicolons ';', and piped commands using '|'.
 * Recognizes input/output redirection symbols ('<', '>', '>>') and associates them
 * with the appropriate filenames.
 *
 * Constructs an array of commands (each with argument vectors) and dispatches the execution.
 * ==============================================================================================
 */


void handle_command(int client_fd, char *command) {
    int num_commands = 10;
    int num_args_per_command = 5;
    int max_arg_length = 20;

    // Allocate memory for input and output redirection flags
    int *input_file_flags = (int *)calloc(num_commands, sizeof(int));
    int *output_file_flags = (int *)calloc(num_commands, sizeof(int));

    // Allocate memory for storing command arguments
    char ***args = (char ***)calloc(num_commands, sizeof(char **));
    if (!args) {
        perror("[ERROR] Memory allocation failed");
        exit(1);
    }

    // Allocate memory for each command and its arguments
    for (int i = 0; i < num_commands; i++) {
        args[i] = (char **)calloc(num_args_per_command, sizeof(char *));
        if (!args[i]) {
            perror("[ERROR] Memory allocation failed");
            exit(1);
        }

        for (int j = 0; j < num_args_per_command; j++) {
            args[i][j] = (char *)calloc(max_arg_length, sizeof(char));
            if (!args[i][j]) {
                perror("[ERROR] Memory allocation failed");
                exit(1);
            }
        }
    }

    int i = 0;
    int j = 0;
    int l = 0;
    char *cur_char = command;
    char **filenames = (char **)calloc(max_arg_length, sizeof(char *));

    // Loop through the command string to parse arguments
    while (*cur_char != '\n') {
        if (*cur_char == ' ') {
            if (l > 0) {
                args[i][j][l] = '\0';
                j++;
                l = 0;
            }
            cur_char++;
            continue;
        }

        if (*cur_char == '<' && *(cur_char + 1) == '<' && *(cur_char + 2) == '<') {
            cur_char += 3;
            continue;
        }

        if (*cur_char == '>' && *(cur_char + 1) == '>') {
            cur_char += 2;
            filenames[i] = read_filename(&cur_char);
            output_file_flags[i] = 2;
            continue;
        }

        if (*cur_char == '>') {  // Handle output redirection
            cur_char++;
            filenames[i] = read_filename(&cur_char);
            output_file_flags[i] = 1;
            continue;
        }
        if (*cur_char == '<') {  // Handle input redirection
            cur_char++;
            filenames[i] = read_filename(&cur_char);
            input_file_flags[i] = 1;
            continue;
        }
        if (*cur_char == ';') {  // Handle command separator (;)
            if (l > 0) {
                args[i][j][l] = '\0';
                j++;
            }
            args[i][j] = NULL;

            execute_command(client_fd, args, filenames, i, input_file_flags, output_file_flags, 1);

            // Reset argument storage for next command
            for (int row = 0; row <= i; row++) {
                for (int coll = 0; coll <= j; coll++) {
                    free(args[row][coll]);
                    args[row][coll] = (char *)calloc(20, sizeof(char));
                }
            }
            i = 0;
            j = 0;
            l = 0;
            free(input_file_flags);
            input_file_flags = (int *)calloc(20, sizeof(int));
            free(output_file_flags);
            output_file_flags = (int *)calloc(20, sizeof(int));

            for (int row = 0; row <= i; row++) {
                free(filenames[i]);
            }
            free(filenames);
            filenames = (char **)calloc(max_arg_length, sizeof(char *));
            cur_char++;
            continue;
        }
        if (*cur_char == '|') {  // Handle piping (|) between commands
            if (l > 0) {
                args[i][j][l] = '\0';
                j++;
            }
            args[i][j] = NULL;
            i++;
            j = 0;
            l = 0;
            cur_char++;
            continue;
        }
        // Store the command argument character by character
        args[i][j][l++] = *(cur_char++);
    }

    if (l > 0) {
        args[i][j][l] = '\0';
        j++;
    }
    args[i][j] = NULL;

    // If no valid command, free memory and return
    if (j == 0 || args[0][0] == NULL) {
        free_args(&args, num_commands, num_args_per_command);
        return;
    }

//    for (int row = 0; row <= i; row++) {
//        for (int coll = 0; coll < num_args_per_command; coll++) {
//            printf("%s ", args[row][coll]);
//            if (args[row][coll] == NULL) break;
//        }
//        printf("\n");
//    }

    // Execute the parsed pipeline
    execute_command(client_fd, args, filenames, i, input_file_flags, output_file_flags, 0);

    // Free allocated memory
    free_args(&args, num_commands, num_args_per_command);
    free(input_file_flags);
    free(output_file_flags);
}


/* ==============================================================================================
 * Connection List Management
 * ==============================================================================================
 * This block defines and manipulates a linked list of currently active client connections.
 * Each node contains:
 *   - connection ID
 *   - socket file descriptor
 *   - PID of the child handling that connection
 *
 * Includes:
 *   - print_connections() — prints active connections for 'stat' command
 *   - find_fd()           — finds a file descriptor given a PID or ID
 *   - add_connection()    — adds a new client connection node
 *   - abort_connection()  — forcefully kills and cleans up a connection by PID or ID
 * ==============================================================================================
 */


typedef struct connection_node {
    int id;
    int fd;
    pid_t pid;
    struct connection_node *next;
} connection_node_t;

void print_connections(connection_node_t *list, char *out, size_t max_size) {
    connection_node_t *curr = list;
    out[0] = '\0';

    while (curr) {
        char line[128];
        snprintf(line, sizeof(line), "ID: %d | PID: %d | FD: %d\n", curr->id, curr->pid, curr->fd);
        strncat(out, line, max_size - strlen(out) - 1);
        curr = curr->next;
    }
}

int find_fd(connection_node_t *connection_list, int sender_pid, int abort_id) {
    connection_node_t *curr = connection_list;
    while (curr) {
        if (sender_pid && curr->pid == sender_pid) {
            return curr->fd;
        }
        if (abort_id && curr->id == abort_id) {
            return curr->fd;
        }
        curr = curr->next;

    }
    return -1;
}

void add_connection(connection_node_t **connection_list, int *next_connection_id, int fd, pid_t pid) {
    connection_node_t *node = malloc(sizeof(connection_node_t));
    node->id = (*next_connection_id)++;
    node->fd = fd;
    node->pid = pid;
    node->next = *connection_list;
    *connection_list = node;
    printf("[INFO] Added connection ID %d (PID %d)\n", node->id, pid);
}

void abort_connection(connection_node_t **connection_list, int id, int pid) {
    connection_node_t *prev = NULL, *curr = *connection_list;

    while (curr) {
        if (curr->id == id || curr->pid == pid) {
            kill(curr->pid, SIGTERM);
            waitpid(curr->pid, NULL, 0);
            close(curr->fd);
            printf("[INFO] Aborted connection ID %d (PID %d)\n", curr->id, curr->pid);
            if (prev) {
                prev->next = curr->next;
            } else {
                *connection_list = curr->next;
            }
            free(curr);
            return;
        } else {
            prev = curr;
            curr = curr->next;
        }
    }
    printf("[WARN] No connection found with ID %d\n", id);
}


/* ==============================================================================================
 * Main Server Loop (Select-Based Event Loop)
 * ==============================================================================================
 * Central event loop for the server. Uses select() to monitor:
 *   - the main server socket (for new incoming connections)
 *   - the control pipe (for inter-process messages)
 *
 * Upon new connection: forks a child process and creates an entry in the connection list.
 * Child process handles communication with the client, including:
 *   - forwarding control messages (stat, abort, quit)
 *   - invoking the shell command handler
 * Parent listens for messages and handles them accordingly, ensuring client status is updated.
 * ==============================================================================================
 */


void main_server_loop(int server_fd) {
    // File descriptor for client connection
    int client_fd;
    // Client address information
    struct sockaddr_storage client_addr;
    // Size of client address structure
    socklen_t client_len = sizeof(client_addr);

    connection_node_t *connection_list = NULL;
    int next_connection_id = 1;

    // Pipe for parent-child communication [0]-read, [1]-write
    int control_pipe[2];
    if (pipe(control_pipe) < 0) {
        perror("[ERROR] Failed to create control pipe");
        exit(1);
    }

    // Set non-blocking mode for pipe read end
    fcntl(control_pipe[0], F_SETFL, O_NONBLOCK);

    while (1) {
        // Prepare file descriptor set for select()
        fd_set read_fds; // Set of file descriptors to monitor
        FD_ZERO(&read_fds); // Clear the set
        FD_SET(server_fd, &read_fds); // Add server socket
        FD_SET(control_pipe[0], &read_fds); // Add pipe read end

        // Find maximum file descriptor number
        int max_fd = (server_fd > control_pipe[0]) ? server_fd : control_pipe[0];

        // Wait for events
        int ready = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (ready < 0) {
            perror("[ERROR] select failed");
            continue;
        }

        // Handle commands from child process
        if (FD_ISSET(control_pipe[0], &read_fds)) {
            char parent_buffer[256];
            ssize_t bytes = read(control_pipe[0], parent_buffer, sizeof(parent_buffer) - 1);
            if (bytes > 0) {
                parent_buffer[bytes] = '\0';
                if (strncmp(parent_buffer, "abort ", 5) == 0) {
                    // Handle 'abort' command
                    strtok(parent_buffer, " ");
                    int arg_id = atoi(strtok(NULL, " "));
                    int sender_pid = atoi(strtok(NULL, " "));

                    // Prepare info message for sender
                    char info_message[256] = "", buffer[512] = "";
                    int total = 0;
                    snprintf(info_message, sizeof(info_message), "[INFO] Aborted connection for ID %d\n", arg_id);
                    if (info_message[0] != '\0') {
                        strncat(buffer, info_message, sizeof(buffer) - strlen(buffer) - 1);
                        total = strlen(buffer);
                    }
                    buffer[total] = '\0';

                    // Find file descriptors: one for the sender client and one for the client that should be disconnected
                    int sender_fd = find_fd(connection_list, sender_pid, -1);
                    int arg_fd = find_fd(connection_list, -1, arg_id);
                    if (sender_fd == arg_fd) {
                        write(arg_fd, "[ABORT]", 7);
                        write(arg_fd, "[END]", 5);
                    } else {
                        if (sender_fd >= 0) {
                            write(sender_fd, buffer, total);
                            write(sender_fd, "[END]", 5);
                        }
                        if (arg_fd >= 0) {
                            write(arg_fd, "[ABORT]", 7);
                            write(arg_fd, "[END]", 5);
                        }
                    }
                    // Abort connection for chosen id
                    abort_connection(&connection_list, arg_id, -1);

                } else if (strncmp(parent_buffer, "stat", 4) == 0) {
                    // Handle 'stat' command
                    int sender_pid = atoi(parent_buffer + 5);
                    char stat_buf[1024] = "";

                    // Write list of connections in buffer
                    print_connections(connection_list, stat_buf, sizeof(stat_buf));

                    // Find file descriptor for the sender client
                    int found_fd = find_fd(connection_list, sender_pid, -1);
                    if (found_fd >= 0) {
                        write(found_fd, stat_buf, strlen(stat_buf));
                        write(found_fd, "[END]", 5);
                    } else {
                        fprintf(stderr, "[WARN] No connection found for PID %d\n", sender_pid);
                    }

                } else if (strncmp(parent_buffer, "quit", 4) == 0) {
                    // Handle 'quit' command
                    int sender_pid = atoi(parent_buffer + 5);

                    // Find file descriptor for the sender client
                    int found_fd = find_fd(connection_list, sender_pid, -1);
                    if (client_fd > 0) {
                        write(found_fd,  "[QUIT]", 6);
                        write(found_fd, "[END]", 5);
                    }

                    // Abort connection for sender client
                    abort_connection(&connection_list, -1, sender_pid);
                }
            }
        }

        // Handle new connection
        if (FD_ISSET(server_fd, &read_fds)) {
            client_len = sizeof(client_addr);
            client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
            if (client_fd < 0) {
                perror("[ERROR] Accept failed");
                continue;
            }

            printf("[INFO] Client connected.\n");

            // Create child process for new client
            pid_t pid = fork();

            if (pid < 0) {
                perror("[ERROR] Fork failed");
                close(client_fd);
                continue;
            }

            if (pid == 0) {
                // Child process code
                close(server_fd);
                close(control_pipe[0]); // Close unused pipe

                char buffer[4096];

                while (1) {
                    int bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
                    if (bytes_read <= 0) {
                        printf("[INFO] Client disconnected (PID %d).\n", getpid());
                        break;
                    }

                    printf("[INFO] (PID %d) Command from client: %s\n", getpid(), buffer);

                    // Forward special commands to parent
                    if (strncmp(buffer, "stat", 4) == 0) {
                        char msg[256];
                        snprintf(msg, sizeof(msg), "stat %d\n", getpid());
                        write(control_pipe[1], msg, strlen(msg));
                    } else if (strncmp(buffer, "abort ", 6) == 0) {
                        char msg[256];
                        snprintf(msg, sizeof(msg), "%s %d\n", buffer, getpid());
                        write(control_pipe[1], msg, strlen(msg));
                    } else if (strncmp(buffer, "quit", 4) == 0) {
                        char msg[256];
                        snprintf(msg, sizeof(msg), "%s %d\n", buffer, getpid());
                        write(control_pipe[1], msg, strlen(msg));
                    } else {
                        buffer[strlen(buffer)] = '\n';
                        handle_command(client_fd, buffer);
                    }
                }

                close(client_fd);
                exit(0);
            } else {
                // Parent process - add to connection list
                add_connection(&connection_list, &next_connection_id, client_fd, pid);
            }
        }
    }
}


/* ==============================================================================================
 * UNIX Socket Server Entrypoint
 * ==============================================================================================
 * Initializes and starts a server using a UNIX domain socket bound to a filesystem path.
 * Handles socket creation, binding, listening, and cleanup.
 * Once ready, the server enters the main event loop to handle client connections.
 * ==============================================================================================
 */


void run_unix_server(char *socket_path) {
    int server_fd;
    struct sockaddr_un server_addr;

    // Remove any existing socket file
    unlink(socket_path);

    // Create UNIX domain socket
    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Unix socket creation failed");
        exit(1);
    }

    // Configure socket address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, socket_path, sizeof(server_addr.sun_path) - 1);

    // Bind the socket to the file path
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Unix bind failed");
        exit(1);
    }

    // Listen for incoming connections
    if (listen(server_fd, 5) < 0) {
        perror("Unix listen failed");
        exit(1);
    }

    printf("[UNIX SERVER] Server is listening on unix socket: %s\n", socket_path);

    main_server_loop(server_fd);

    // Cleanup
    close(server_fd);
    unlink(socket_path);
}


/* ==============================================================================================
 * TCP Socket Server Entrypoint
 * ==============================================================================================
 * Initializes and starts a server using TCP/IP sockets.
 * Binds the socket to a specified IP address and port, enabling remote client access.
 * Configures options like SO_REUSEADDR and prepares the socket to listen.
 * Enters the main server loop once setup is complete.
 * ==============================================================================================
 */


void run_tcp_server(const char *host, int port) {
    int server_fd;
    struct sockaddr_in server_addr;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("TCP socket creation failed");
        exit(1);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (host == NULL) {
        server_addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
            perror("[SERVER] Invalid IP address");
            close(server_fd);
            exit(1);
        }
    }

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("TCP bind failed");
        exit(1);
    }

    if (listen(server_fd, 5) < 0) {
        perror("TCP listen failed");
        exit(1);
    }

    printf("[TCP SERVER] Listening on %s:%d...\n", host, port);

    main_server_loop(server_fd);


    close(server_fd);
}