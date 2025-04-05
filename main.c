#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "server.h"
#include "client.h"

#define SOCKET_PATH "/tmp/myshell_socket"


void run_script(const char *filename){
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("[ERROR] Cannot open script file");
        return;
    }
    char command[1024];
    while (fgets(command, sizeof(command), file)) {
        if (strlen(command) > 0) {
            handle_command(-1, command);
        }
    }

    fclose(file);
}


int main(int argc, char *argv[]) {
    int is_server = 1, is_client = 0;
    char *command_to_run = NULL;
    char *socket_path = SOCKET_PATH;
    char *host = "127.0.0.1";
    int tcp_port = -1;
    int opt;

    while ((opt = getopt(argc, argv, "scu:p:h:i:")) != -1) {
        switch (opt) {
            case 's':
                is_server = 1;
                is_client = 0;
                break;
            case 'c':
                is_client = 1;
                is_server = 0;
                break;
            case 'u':
                socket_path = optarg;
                break;
            case 'p':
                tcp_port = atoi(optarg);
                break;
            case 'i':
                host = optarg;
                break;
            case 'h':
                printf("Usage: %s [-s | -c] [-u path/to/socket | -p tcp_port] [-i host_ip] [-c command...]\n", argv[0]);
                return 0;
            default:
                return 1;
        }
    }

    if (is_client && optind < argc && argv[optind][0] != '-') {
        size_t total_len = 0;
        for (int i = optind; i < argc; i++) {
            total_len += strlen(argv[i]) + 1;
        }

        command_to_run = malloc(total_len);
        if (!command_to_run) {
            perror("malloc");
            exit(1);
        }

        command_to_run[0] = '\0';
        for (int i = optind; i < argc; i++) {
            strcat(command_to_run, argv[i]);
            if (i < argc - 1) strcat(command_to_run, " ");
        }
    }

    if (is_server && optind < argc) {
        const char *script_file = argv[optind];
        run_script(script_file);
        return 0;
    }

    if (command_to_run) {
        size_t len = strlen(command_to_run);
        char command_buf[512];
        snprintf(command_buf, sizeof(command_buf), "%s%s", command_to_run,
                 (command_to_run[len - 1] == '\n') ? "" : "\n");

        handle_command(-1, command_buf);
        free(command_to_run);
        return 0;
    }

    if (is_server) {
        if (tcp_port > 0) {
            run_tcp_server(host, tcp_port);
        } else {
            run_unix_server(socket_path);
        }
    } else if (is_client) {
        if (tcp_port > 0) {
            run_tcp_client(host, tcp_port);
        } else {
            run_unix_client(socket_path);
        }
    }

    return 0;
}
