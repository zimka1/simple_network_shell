#ifndef MYSHELL_SERVER_H
#define MYSHELL_SERVER_H


void execute_pipeline(int client_fd, char ***args, char **filenames, int row_number, int *input_file_flags, int *output_file_flags);
void handle_command(int client_fd, char *command);
void output_redirection(char *filename);
void input_redirection(char *filename);
void run_unix_server(char *socket_path);
void run_tcp_server(const char *host, int port);

#endif //MYSHELL_SERVER_H
