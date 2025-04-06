#ifndef MYSHELL_SERVER_H
#define MYSHELL_SERVER_H


void handle_command(int client_fd, char *command);
void run_unix_server(char *socket_path);
void run_tcp_server(const char *host, int port);

#endif //MYSHELL_SERVER_H
