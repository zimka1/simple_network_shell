#ifndef MYSHELL_CLIENT_H
#define MYSHELL_CLIENT_H

#include <stdio.h>

void get_prompt(char *prompt, size_t size);
void run_unix_client(char *socket_path);
void run_tcp_client(const char *host, int port);
#endif //MYSHELL_CLIENT_H
