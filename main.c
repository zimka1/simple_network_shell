#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>
#include <time.h>
#include <string.h>
#include <fcntl.h>


void get_prompt(char *prompt, size_t size) {
    // Get the username of the current user
    struct passwd *pw = getpwuid(getuid());

    // Buffer to store the hostname
    char hostname[256];

    // Get the current timestamp
    time_t now = time(NULL);

    // Convert the timestamp to local time structure
    struct tm *tm_info = localtime(&now);

    // Buffer to store formatted time (HH:MM)
    char time_buf[6];

    // Retrieve the hostname of the machine
    gethostname(hostname, sizeof(hostname));

    // Format the time as HH:MM and store it in time_buf
    strftime(time_buf, sizeof(time_buf), "%H:%M", tm_info);

    // Format the prompt string: "HH:MM username@hostname#"
    snprintf(prompt, size,
             "\033[33m%s\033[0m "
             "\033[32m%s\033[0m@"
             "\033[34m%s\033[0m# ",
             time_buf, pw->pw_name, hostname);
}

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

void execute_pipeline(char ***args, char **filename, int row_number, int *input_file_flags, int *output_file_flags) {
    int pipes[row_number][2];
    pid_t pids[row_number + 1];

    // Create pipes
    for (int i = 0; i < row_number; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("Pipe error");
            exit(1);
        }
    }


    for (int i = 0; i <= row_number; i++) {
        pids[i] = fork();
        if (pids[i] == 0){
            if (i == 0) {  // The first process (cmd1)
                if (input_file_flags[i]) input_redirection(filename[i]);
                if (output_file_flags[i]) {
                    close(pipes[i][0]); // Doesn't read from pipe
                    close(pipes[i][1]); // Doesn't write in pipe
                    output_redirection(filename[i]);
                }
                else {
                    dup2(pipes[i][1], STDOUT_FILENO); // write in the first pipe
                    close(pipes[i][0]); // Doesn't read from pipe
                }
            } else if (i == row_number) {  // Last process (cmdN)
                if (output_file_flags[i]) output_redirection(filename[i]);
                if (input_file_flags[i]) {
                    close(pipes[i - 1][0]); // Doesn't read from pipe
                    close(pipes[i - 1][1]); // Doesn't write in pipe
                    input_redirection(filename[i]);
                }
                else {
                    dup2(pipes[i - 1][0], STDIN_FILENO); // read from previous pipe
                    close(pipes[i - 1][1]); // Doesn't write
                }
            } else {  // Middle processes (cmd2, cmd3, ..., cmdN-1)
                if (input_file_flags[i]) {
                    close(pipes[i - 1][0]); // Doesn't read from pipe
                    input_redirection(filename[i]);
                } else
                    dup2(pipes[i - 1][0], STDIN_FILENO); // read from previous pipe

                if (output_file_flags[i]) {
                    close(pipes[i][1]); // Doesn't write in pipe
                    output_redirection(filename[i]);
                } else
                    dup2(pipes[i][1], STDOUT_FILENO); // write in the first pipe

                close(pipes[i - 1][1]); // close unnecessary pipe
                close(pipes[i][0]); // close unnecessary pipe
            }

            // Close all pipes in child process
            for (int j = 0; j < row_number; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            // Replace the current process with the desired command
            execvp(args[i][0], args[i]);
            perror("Execution error");
            exit(1);
        } else if (pids[i] < 0) {
            perror("Fork error");
            exit(1);
        }
    }

    // Close all pipes to avoid leaks
    for (int i = 0; i < row_number; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    // The parent is waiting for the completion of all processes
    for (int i = 0; i <= row_number; i++) {
        waitpid(pids[i], NULL, 0);
    }

}

char* read_filename(char **cur_char){
    char *filename = (char *)calloc(20, sizeof(char));

    char *cur_char_filename = filename;

    while (**cur_char != ';' && **cur_char != '\n' && **cur_char != '\0') {
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

void handle_command(char *command) {
    int num_commands = 10;
    int num_args_per_command = 5;
    int max_arg_length = 20;

    // Allocate memory for input and output redirection flags
    int *input_file_flags = (int *)calloc(num_commands, sizeof(int));
    int *output_file_flags = (int *)calloc(num_commands, sizeof(int));

    // Allocate memory for storing command arguments
    char ***args = (char ***)calloc(num_commands, sizeof(char **));
    if (!args) {
        perror("Memory allocation failed");
        exit(1);
    }

    // Allocate memory for each command and its arguments
    for (int i = 0; i < num_commands; i++) {
        args[i] = (char **)calloc(num_args_per_command, sizeof(char *));
        if (!args[i]) {
            perror("Memory allocation failed");
            exit(1);
        }

        for (int j = 0; j < num_args_per_command; j++) {
            args[i][j] = (char *)calloc(max_arg_length, sizeof(char));
            if (!args[i][j]) {
                perror("Memory allocation failed");
                exit(1);
            }
        }
    }

    int i = 0;
    int j = 0;
    int l = 0;
    char *cur_char = command;
    char **filename = (char **)calloc(max_arg_length, sizeof(char *));

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
            filename[i] = read_filename(&cur_char);
            output_file_flags[i] = 1;
            continue;
        }
        if (*cur_char == '<') {  // Handle input redirection
            cur_char++;
            filename[i] = read_filename(&cur_char);
            input_file_flags[i] = 1;
            continue;
        }
        if (*cur_char == ';') {  // Handle command separator (;)
            if (l > 0) {
                args[i][j][l] = '\0';
                j++;
            }
            args[i][j] = NULL;
            printf("%d, %d, %d \n", i, j, l);
            for (int coll = 0; coll <= j; coll++) {
                printf("%s \n", args[i][coll]);
            }
            execute_pipeline(args, filename, i, input_file_flags, output_file_flags);

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
                free(filename[i]);
            }
            free(filename);
            filename = (char **)calloc(max_arg_length, sizeof(char *));
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
    execute_pipeline(args, filename, i, input_file_flags, output_file_flags);

    // Free allocated memory
    free_args(&args, num_commands, num_args_per_command);
    free(input_file_flags);
    free(output_file_flags);
}




int main(int argc, char *argv[]) {
    int is_server = 0, is_client = 0;
    char *socket_path = NULL;
    int port = 0;
    int opt;

    while ((opt = getopt(argc, argv, "scp:u:h")) != -1) {
        switch (opt) {
            case 's':
                is_server = 1;
                break;
            case 'c':
                is_client = 1;
                break;
            case 'p':
                port = atoi(optarg);
                break;
            case 'u':
                socket_path = optarg;
                break;
            case 'h':
                return 0;
            default:
                return 1;
        }
    }

    char prompt[256];
    char command[1024];

    while (1) {
        get_prompt(prompt, sizeof(prompt));
        printf("%s ", prompt);
        fflush(stdout);

        if (!fgets(command, sizeof(command), stdin)) {
            printf("\nВыход.\n");
            break;
        }

        handle_command(command);
    }

    return 0;
}
