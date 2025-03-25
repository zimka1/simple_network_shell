#ifndef MYSHELL_SERVER_H
#define MYSHELL_SERVER_H


void execute_command(int client_fd, char ***args, char **filenames, int row_number, int *input_file_flags, int *output_file_flags);
void handle_command(int client_fd, char *command);
void run_unix_server(char *socket_path);
void run_tcp_server(const char *host, int port);

#endif //MYSHELL_SERVER_H
