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
                printf("Usage: %s [-s | -c] [-u path/to/socket | -p tcp_port] [-i host_ip]\n", argv[0]);
                return 0;
            default:
                return 1;
        }
    }

    if (optind < argc) {
        const char *script_file = argv[optind];
        run_script(script_file);
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