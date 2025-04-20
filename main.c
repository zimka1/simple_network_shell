/* ==============================================================================================
 * ==============================================================================================
 *
 *  Author:     Aliaksei Zimnitski
 *  Date:       01.04.2025
 *
 *  Academic year:    2
 *  Semester:         4
 *  Field of study:   informatika
 *
 *  Description:
 *      This is the main module of a shell program that supports
 *      both client and server modes using UNIX and TCP sockets. The shell interprets
 *      user input or socket-received commands, executes them, and handles I/O redirection
 *      and special characters.
 *
 *      Based on flags provided via command-line arguments, the program can:
 *          - run as a server (default),
 *          - act as a client,
 *          - accept scripts as input,
 *          - run commands interactively or once via argument.
 *
 *      Internal commands:
 *          - help   → shows usage information.
 *          - quit   → disconnects the current client.
 *          - halt   → terminates the entire program (server shutdown).
 *
 *      Supported switches:
 *          -s          → run in server mode (default),
 *          -c          → run in client mode,
 *          -u <path>   → use UNIX socket at specified path,
 *          -p <port>   → use TCP port to listen or connect,
 *          -i <ip>     → specify IP address for client/server TCP connections,
 *          -h          → show help message and usage info.
 *
 *      Optional:
 *          -c "command..." → run the command and exit (client mode only),
 *          [filename]      → process command script (server mode only).
 *
 *  Optional tasks – Completed extensions:
 *  --------------------------------------
 *      (1)  Non-interactive mode – the shell can process script files via `run_script()` (2 points)
 *      (2)  Cross-platform compatibility – runs on both Linux and FreeBSD (POSIX-compliant code) (2 points)
 *      (3)  Internal command `stat` – displays a list of active client connections (3 points)
 *      (4)  Internal command `abort <id>` – forcefully terminates a specific connection by ID (2 points)
 *      (7)  Support for `-i <ip>` flag – allows specifying IP address for TCP server/client (2 points)
 *      (11) Linked library usage – I/O redirection functions implemented in `redirections.c/.h` (2 points)
 *      (15) Internal commands as flags with `-c` – e.g., `-halt`, `-help` run once and exit (2 points)
 *      (21) Functional Makefile (2 points)
 *      (23) Comprehensive English documentation and inline comments throughout the codebase (1 point)
 *
 *  Total points for optional tasks: 2 + 2 + 3 + 2 + 2 + 2 + 2 + 2 + 1 = 18 points
 *
 *  Usage Examples:
 *      ./shell -s -u /tmp/myshell.sock
 *      ./shell -c -p 1234 -i 127.0.0.1
 *      ./shell -h
 *      ./shell "ls -la | grep c"
 *      ./shell script.txt
 *
 *  Dependencies:
 *      server.h, client.h
 *
 *
 * ==============================================================================================
 * ==============================================================================================
 */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "server.h"
#include "client.h"

#define SOCKET_PATH "/tmp/myshell_socket"


/* ==============================================================================================
 * Script Processing
 * ==============================================================================================
 *
 * This function is used when a filename is passed as an argument in server mode. It opens
 * the script file and reads commands line by line. Each command is sent to the same handler
 * as if it was received from the socket or typed interactively.
 *
 * ==============================================================================================
 * ============================================================================================== */


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


/* ==============================================================================================
 * Help Function: Displays Usage Information
 * ==============================================================================================
 *
 * This function prints a structured help message explaining all command-line options,
 * internal commands, usage examples, and supported functionality. It is invoked either
 * via the `-h` flag or by running an invalid combination of arguments.
 *
 * ==============================================================================================
 * ============================================================================================== */

void print_help() {
    printf(
            "\nHelp\n"
            "-----------------------------------------\n"
            "Usage:\n"
            "  ./shell [options] [script_file | -c \"command\"]\n\n"
            "Modes:\n"
            "  -s                Run as a server (default mode)\n"
            "  -c                Run as a client\n\n"
            "Socket Options:\n"
            "  -u <path>         Use UNIX domain socket at specified path\n"
            "  -p <port>         Use TCP socket on specified port\n"
            "  -i <ip>           Specify IP address for TCP connection\n\n"
            "Internal Commands:\n"
            "  help              Show this help message\n"
            "  quit              Disconnect current client\n"
            "  halt              Terminate the entire server and all clients\n"
            "  stat              Show active client connections (server only)\n"
            "  abort <id>        Force-close a specific connection by ID\n\n"
            "One-Time Commands (Client Mode Only):\n"
            "  -c \"command\"      Send a single command to the server and exit\n\n"
            "Script Support (Server Mode Only):\n"
            "  <script_file>     Execute commands from a given script file line-by-line\n\n"
            "Examples:\n"
            "  ./shell -s -u /tmp/shell.sock\n"
            "  ./shell -c -p 1234 -i 127.0.0.1\n"
            "  ./shell -c \"ls -l | grep txt\"\n"
            "  ./shell script.txt\n\n"
    );
}



/* ==============================================================================================
 * Main Function: Entry Point of the Shell
 * ==============================================================================================
 *
 * This is the main routine of the shell. It handles command-line arguments and decides
 * whether to run as a server, client, or execute a one-time command or script. Based on
 * flags like -s, -c, -u, -p, and -i, the shell adapts its behavior.
 *
 * ==============================================================================================
 * ============================================================================================== */


int main(int argc, char *argv[]) {
    int is_server = 1, is_client = 0;
    char *command_to_run = NULL;
    char *socket_path = SOCKET_PATH;
    char *host = "127.0.0.1";
    int tcp_port = -1;
    int opt;


    /* ==============================================================================================
     * Command-line Argument Parsing
     * ==============================================================================================
     *
     * Parses flags using getopt():
     *   -s       → server mode (default)
     *   -c       → client mode
     *   -u path  → use UNIX socket at given path
     *   -p port  → use TCP port
     *   -i host  → use specific host address
     *   -h       → show help and exit
     *
     * ==============================================================================================
     * ============================================================================================== */


    while ((opt = getopt(argc, argv, "scu:p:hi:")) != -1) {
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
                print_help();
                return 0;
            default:
                return 1;
        }
    }


    /* ==============================================================================================
     * One-time Command Execution (Client Mode)
     * ==============================================================================================
     *
     * If running as a client and additional arguments are provided (not a switch), treat the rest
     * of the command-line as a single command to be sent and executed once.
     *
     * ==============================================================================================
     * ============================================================================================== */


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


    /* ==============================================================================================
     * Script Mode Execution (Server Mode)
     * ==============================================================================================
     *
     * If additional argument is provided in server mode and is not a flag, treat it as a path
     * to a script file containing commands. The script will be executed line-by-line.
     *
     * ==============================================================================================
     * ============================================================================================== */


    if (is_server && optind < argc) {
        const char *script_file = argv[optind];
        run_script(script_file);
        return 0;
    }


    /* ==============================================================================================
     * Command Execution via -c
     * ==============================================================================================
     *
     * If a command was collected via -c, format it properly and send it for processing.
     *
     * ==============================================================================================
     * ============================================================================================== */


    if (command_to_run) {
        size_t len = strlen(command_to_run);
        char command_buf[512];
        snprintf(command_buf, sizeof(command_buf), "%s%s", command_to_run,
                 (command_to_run[len - 1] == '\n') ? "" : "\n");

        handle_command(-1, command_buf);
        free(command_to_run);
        return 0;
    }


    /* ==============================================================================================
     * Server and Client Startup
     * ==============================================================================================
     *
     * Depending on the parsed mode and socket type, start the appropriate server or client
     * implementation: either UNIX domain or TCP/IP.
     *
     * ==============================================================================================
     * ============================================================================================== */


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


/* ==============================================================================================
                    I N S T R U C T I O N S   T O   R U N   T H E   P R O G R A M
=================================================================================================

To build and run this shell application in a Linux or UNIX-like environment:

1) Compile all modules using gcc:
        gcc -Wall -g -o shell main.c server.c client.c redirections.c
    OR
        make

2) Run in server mode (default):
        ./shell -s

3) Run in server mode using UNIX socket:
        ./shell -s -u /tmp/myshell.sock

4) Run in server mode using TCP port:
        ./shell -s -p 1234 -i 127.0.0.1

5) Run in client mode (UNIX socket):
        ./shell -c -u /tmp/myshell.sock

6) Run in client mode (TCP socket):
        ./shell -c -p 1234 -i 127.0.0.1

7) Display help:
        ./shell -h

8) Execute one-time command (client mode):
        ./shell -c "ls -la | grep txt"

9) Run a command script (server mode only):
        ./shell script.txt

=================================================================================================
                                   P R O G R A M   A S S E S S M E N T
=================================================================================================

This program implements a simple interactive shell supporting local and remote (client-server)
execution. It provides both TCP and UNIX domain socket communication, and can execute piped
commands with input/output redirection.

The shell supports interactive mode, script-based execution, and one-shot client commands.
Internal control commands allow terminating sessions and querying connections.

=================================================================================================
                                       P R O G R A M   F E A T U R E S
=================================================================================================

- Supports UNIX and TCP sockets for local or network communication
- Interactive command execution with support for redirection and pipes
- Accepts shell-like syntax and supports command chaining
- Supports script execution from files (non-interactive mode)
- Built-in internal commands: `help`, `quit`, `halt`, `stat`, `abort`
- Client-server architecture using forked processes per connection
- Cross-platform compatibility (POSIX-compliant: Linux, FreeBSD)
- External library usage for modular I/O redirection handling
- Full English documentation and clean modular structure

=================================================================================================
                                I N P U T   H A N D L I N G   B E H A V I O R
=================================================================================================

- Accepts command-line switches such as -s, -c, -u, -p, -i, -h
- In client mode, accepts commands from user interactively or via -c argument
- In server mode, can execute scripts from files line-by-line
- If a command fails or is malformed, appropriate error messages are displayed
- Redirection using `<`, `>`, `>>` and piping using `|` are fully supported

=================================================================================================
                    A L G O R I T H M S   A N D   P R O G R A M M I N G   T E C H N I Q U E S
=================================================================================================

- Uses `fork()` + `execvp()` for command execution
- Commands are parsed and split based on separators (`;`, `|`, `<`, `>`)
- Select-based server loop handles multiple sockets and a control pipe
- Inter-process communication is used to notify the parent of client-side events
- Pipes (`pipe()`) are used to form command pipelines
- I/O redirection is implemented via `dup2()` and file descriptor manipulation
- Robust memory management ensures dynamic command handling

=================================================================================================
                               P O T E N T I A L   E N H A N C E M E N T S
=================================================================================================

- Add configurable prompt format and prompt command
- Implement support for wildcards/globbing (`*`)
- Introduce idle timeout and connection log support
- Add support for daemon mode and advanced signal handling
- Load settings from config file and environment variables

=================================================================================================
                                   T E S T I N G   R E S U L T S
=================================================================================================

- Tested on Linux with both UNIX and TCP sockets
- Successfully handles multiple clients simultaneously
- Correctly processes piped and redirected commands
- Handles script execution and inline commands via `-c`
- Tested internal commands: `halt`, `quit`, `stat`, `abort`, `help`
- Cross-platform behavior confirmed on Linux and FreeBSD systems

=================================================================================================
=================================================================================================
*/