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
             "\033[34m%s\033[0m#",
             time_buf, pw->pw_name, hostname);
}

void output_redirection(char *filename) {
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        perror("Error opening file");
        exit(1);
    }
    dup2(fd, STDOUT_FILENO);
    close(fd);
}

void input_redirection(char *filename){
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        perror("Error opening input file");
        exit(1);
    }
    dup2(fd, STDIN_FILENO);
    close(fd);
}
void execute_command(char **args, char *filename, int input_file_flag, int output_file_flag) {
    if (!args[0] || strlen(args[0]) == 0) return;

    if (strcmp(args[0], "quit") == 0) {
        printf("Exiting shell.\n");
        exit(0);
    }

    pid_t pid = fork();

    if (pid == 0) {
        if (filename && output_file_flag) {
            output_redirection(filename);
        }
        if (filename && input_file_flag) {
            input_redirection(filename);
        }
        execvp(args[0], args);
        perror("Execution error");
        exit(1);
    } else if (pid > 0) {
        waitpid(pid, NULL, 0);
    } else {
        perror("Fork error");
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

void free_args(char ***args, int num_args) {
    for (int k = 0; k < num_args; k++) {
        free((*args)[k]);
    }
    free((*args));
    *args = NULL;
}

void handle_command(char *command) {
    int num_args = 64;
    char **args = (char **)calloc(num_args, sizeof(char *));
    int input_file_flag = 0;
    int output_file_flag = 0;
    if (!args) {
        printf("Memory allocation failed for args!\n");
        exit(1);
    }

    for (int k = 0; k < num_args; k++) {
        args[k] = (char *)calloc(10, sizeof(char));
        if (!args[k]) {
            printf("Memory allocation failed for args[%d]!\n", k);
            exit(1);
        }
    }

    int i = 0;
    int j = 0;
    char *cur_char = command;
    char *filename = NULL;

    while (*cur_char != '\n') {
        if (*cur_char == ' ') {
            if (j > 0) {
                args[i][j] = '\0';
                i++;
                j = 0;
            }
            cur_char++;
            continue;
        }
        if (*cur_char == ';') {
            if (j > 0) {
                args[i][j] = '\0';
                i++;
            }

            args[i] = NULL;
            execute_command(args, filename, input_file_flag, output_file_flag);
            filename = NULL;

            for (int k = 0; k <= i; k++) {
                free(args[k]);
                args[k] = (char *)calloc(10, sizeof(char));
            }

            i = 0;
            j = 0;
            input_file_flag = 0;
            output_file_flag = 0;
            cur_char++;
            continue;
        }
        if (*cur_char == '>') {
            cur_char++;
            filename = read_filename(&cur_char);
            output_file_flag = 1;
            continue;
        }
        if (*cur_char == '<') {
            cur_char++;
            filename = read_filename(&cur_char);
            input_file_flag = 1;
            continue;
        }

        args[i][j++] = *(cur_char++);
    }

    if (j > 0) {
        args[i][j] = '\0';
        i++;
    }
    args[i] = NULL;

    if (i == 0 || args[0] == NULL) {
        free_args(&args, num_args);
        return;
    }

    execute_command(args, filename, input_file_flag, output_file_flag);

    filename = NULL;

    free_args(&args, num_args);
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
