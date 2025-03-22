#include <stdio.h>
#include <unistd.h>
#include "server.h"
#include "client.h"

#define SOCKET_PATH "/tmp/myshell_socket"


int main(int argc, char *argv[]) {
    int is_server = 1, is_client = 0;
    char *socket_path = SOCKET_PATH;
    int opt;

    while ((opt = getopt(argc, argv, "scu:h")) != -1) {
        switch (opt) {
            case 's':
                is_server = 1;
                break;
            case 'c':
                is_client = 1;
                is_server = 0;
            case 'u':
                socket_path = optarg;
                break;
            case 'h':
                printf("Use: %s [-s | -c] [-u path/to/socket]\n", argv[0]);
                return 0;
            default:
                return 1;
        }
    }

    if (is_server) {
        run_server(socket_path);
    } else if (is_client) {
        run_client(socket_path);
    }

    return 0;
}