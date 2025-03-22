#ifndef MYSHELL_CLIENT_H
#define MYSHELL_CLIENT_H

#include <stdio.h>

void get_prompt(char *prompt, size_t size);
void run_client(char *socket_path);

#endif //MYSHELL_CLIENT_H
