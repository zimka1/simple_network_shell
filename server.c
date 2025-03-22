#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>


void output_redirection(char *filename) {
    // Open (or create) a file for writing
    // O_WRONLY  - open for writing only
    // O_CREAT   - create the file if it does not exist
    // O_TRUNC   - truncate the file if it already exists
    // 0644      - file permissions: owner can read/write, others can only read
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        perror("Error opening file");
        exit(1);
    }
    // Redirect standard output (stdout) to the file
    // Now, everything written to stdout will be saved in the file
    dup2(fd, STDOUT_FILENO);
    // Close the file descriptor as it is no longer needed
    close(fd);
}

void input_redirection(char *filename){
    // Open the file for reading
    // O_RDONLY - open for read-only access
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        perror("Error opening input file");
        exit(1);
    }
    // Redirect standard input (stdin) to read from the file
    // Now, everything that would be read from stdin will come from the file
    dup2(fd, STDIN_FILENO);
    // Close the file descriptor as it is no longer needed
    close(fd);
}



void execute_pipeline(int client_fd, char ***args, char **filenames, int row_number, int *input_file_flags, int *output_file_flags) {

    // Read result from result_pipe
    char buffer[8192];
    memset(buffer, 0, sizeof(buffer));
    int total = 0, bytes_read;

    char info_message[256] = "";


    if (strcmp(args[0][0], "halt") == 0) {
        snprintf(info_message, sizeof(info_message), "[HALT]");
        if (info_message[0] != '\0') {
            strncat(buffer, info_message, sizeof(buffer) - strlen(buffer) - 1);
            total = strlen(buffer);
        }
        buffer[total] = '\0';
        // Write to client
        write(client_fd, buffer, total);
        printf("Exiting shell.\n");
        exit(0);
    }

    if (strcmp(args[0][0], "quit") == 0) {
        snprintf(info_message, sizeof(info_message), "[QUIT]");
        if (info_message[0] != '\0') {
            strncat(buffer, info_message, sizeof(buffer) - strlen(buffer) - 1);
            total = strlen(buffer);
        }
        buffer[total] = '\0';
        // Write to client
        write(client_fd, buffer, total);
    }

    if (strcmp(args[0][0], "cd") == 0) {
        if (args[0][1] == NULL) {
            snprintf(info_message, sizeof(info_message), "[ERROR] cd: missing argument\n");
        } else if (chdir(args[0][1]) != 0) {
            snprintf(info_message, sizeof(info_message), "[ERROR] cd: directory doesn't exist\n");
        } else {
            snprintf(info_message, sizeof(info_message), "[INFO] Changed directory to: %s\n", args[0][1]);
        }
        if (info_message[0] != '\0') {
            strncat(buffer, info_message, sizeof(buffer) - strlen(buffer) - 1);
        }
        total = strlen(buffer);
        buffer[total] = '\0';
        // Write to client
        write(client_fd, buffer, total);
        printf("%s\n", buffer);
        return;
    }

    int pipes[row_number][2];
    pid_t pids[row_number + 1];
    int result_pipe[2];

    if (pipe(result_pipe) == -1) {
        perror("[ERROR] result_pipe error");
        exit(1);
    }

    for (int i = 0; i < row_number; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("[ERROR] Pipe error");
            exit(1);
        }
    }

    for (int i = 0; i <= row_number; i++) {
        pids[i] = fork();
        if (pids[i] == 0) {
            // CHILD PROCESS

            // Input redirection
            if (input_file_flags[i]) {
                input_redirection(filenames[i]);
            } else if (i > 0) {
                dup2(pipes[i - 1][0], STDIN_FILENO);
            }

            // Output redirection
            if (output_file_flags[i]) {
                output_redirection(filenames[i]);
            } else if (i < row_number) {
                dup2(pipes[i][1], STDOUT_FILENO);
            } else {
                dup2(result_pipe[1], STDOUT_FILENO); // last process writes to result_pipe
            }

            dup2(STDOUT_FILENO, STDERR_FILENO);

            // Close all pipe ends after dup2
            for (int j = 0; j < row_number; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            // Close unused ends of result_pipe
            close(result_pipe[0]);
            if (i != row_number || output_file_flags[i]) {
                close(result_pipe[1]);
            }

            execvp(args[i][0], args[i]);
            perror("[ERROR] Execution error");
            exit(1);
        } else if (pids[i] < 0) {
            perror("[ERROR] Fork error");
            exit(1);
        }
    }

    // PARENT PROCESS

    // Close all pipe ends in parent
    for (int i = 0; i < row_number; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    close(result_pipe[1]); // parent only reads

    // Wait for all children
    for (int i = 0; i <= row_number; i++) {
        waitpid(pids[i], NULL, 0);
    }


    while ((bytes_read = read(result_pipe[0], buffer + total, sizeof(buffer) - total - 1)) > 0) {
        total += bytes_read;
    }

    if (!total) {
        int filename_index = -1;
        for (int i = 0; i <= row_number; i++)
            if (output_file_flags[i]) {filename_index = i; break;}
        if (filename_index != -1) {
            snprintf(info_message, sizeof(info_message), "[INFO] Output saved to file: %s\n", filenames[filename_index]);
        }
    }

    if (info_message[0] != '\0') {
        strncat(buffer, info_message, sizeof(buffer) - strlen(buffer) - 1);
        total = strlen(buffer);
    }

    buffer[total] = '\0';
    close(result_pipe[0]);

    // Write to client
    write(client_fd, buffer, total);
}


char* read_filename(char **cur_char){
    char *filename = (char *)calloc(20, sizeof(char));

    char *cur_char_filename = filename;

    while (**cur_char != ';' && **cur_char != '\n' && **cur_char != '\0') {
        if (**cur_char == ' ') {
            (*cur_char)++;
            continue;
        }
        *(cur_char_filename++) = **cur_char;
        (*cur_char)++;
    }

    return filename;
}


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

            execute_pipeline(client_fd, args, filenames, i, input_file_flags, output_file_flags);

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

    // Execute the parsed pipeline
    execute_pipeline(client_fd, args, filenames, i, input_file_flags, output_file_flags);

    // Free allocated memory
    free_args(&args, num_commands, num_args_per_command);
    free(input_file_flags);
    free(output_file_flags);
}


void main_server_loop(int server_fd) {
    int client_fd;
    struct sockaddr_storage client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[1024];

    while (1) {
        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("[ERROR] Accept failed");
            continue;
        }

        printf("[INFO] Client connected.\n");

        while (1) {
            int bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
            if (bytes_read <= 0) {
                printf("[INFO] Client disconnected.\n");
                break;
            }

            buffer[bytes_read] = '\0';
            printf("[INFO] Command from client: %s\n", buffer);

            handle_command(client_fd, buffer);
        }

        close(client_fd);
    }
}



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


void run_tcp_server(int port) {
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
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("TCP bind failed");
        exit(1);
    }

    if (listen(server_fd, 5) < 0) {
        perror("TCP listen failed");
        exit(1);
    }

    printf("[TCP SERVER] Listening on port %d...\n", port);

    main_server_loop(server_fd);


    close(server_fd);
}
