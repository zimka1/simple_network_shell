/* ==============================================================================================
 * Client Module
 * ==============================================================================================
 *
 * This module implements the client-side logic for the interactive shell.
 * It supports connecting to either a UNIX socket or a TCP socket and communicating
 * with the server through bidirectional streams. The user enters commands which
 * are sent to the server; server responses are printed to stdout.
 *
 * The prompt includes time, username, and hostname. The client also supports heredoc (<<).
 * Built-in control signals like [HALT], [QUIT], and [ABORT] are processed internally.
 *
 * ==============================================================================================
 * ============================================================================================== */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>
#include <time.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <fcntl.h>


/* ==============================================================================================
 * Generate Shell Prompt
 * ==============================================================================================
 *
 * Constructs a colored prompt string of the form:
 *     HH:MM username@hostname#
 * Using system functions: getpwuid, gethostname, time, localtime, strftime.
 * Colors are added via ANSI escape codes.
 *
 * ==============================================================================================
 * ============================================================================================== */


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
             "\033[34m%s\033[0m# ",
             time_buf, pw->pw_name, hostname);
}


/* ==============================================================================================
 * Main Client Interaction Loop
 * ==============================================================================================
 *
 * Handles bidirectional communication between client and server.
 * - Reads user input and sends it to the server.
 * - Receives and prints server response.
 * - Supports heredoc (<< delimiter), which is rewritten into a printf-pipe.
 * - Non-blocking I/O is used for responsiveness.
 * - Recognizes control messages like [HALT], [QUIT], and [ABORT].
 *
 * ==============================================================================================
 * ============================================================================================== */


void main_connection_loop(int sock) {
    char prompt[256];            // Prompt string (e.g., "12:30 user@host# ")
    char buffer[4096];            // Buffer for incoming server data
    char input_buf[1024];        // Buffer for user input from stdin
    int waiting_for_response = 0; // Flag: true if a command has been sent and waiting for output

    // Set the socket and stdin to non-blocking mode
    fcntl(sock, F_SETFL, O_NONBLOCK);
    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);

    // Initial display of the prompt
    get_prompt(prompt, sizeof(prompt));
    printf("%s", prompt);
    fflush(stdout);

    while (1) {
        fd_set read_fds;
        FD_ZERO(&read_fds);

        // Wait for input from user (stdin) only if not already waiting for server response
        if (!waiting_for_response)
            FD_SET(STDIN_FILENO, &read_fds);

        // Always check for server messages
        FD_SET(sock, &read_fds);

        int max_fd = sock > STDIN_FILENO ? sock : STDIN_FILENO;

        // Wait for activity on stdin or socket (blocking select)
        int ready = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (ready < 0) {
            perror("[CLIENT] select failed");
            break;
        }

        // USER INPUT HANDLING
        if (!waiting_for_response && FD_ISSET(STDIN_FILENO, &read_fds)) {
            // Read line from stdin
            if (!fgets(input_buf, sizeof(input_buf), stdin)) {
                printf("[CLIENT] Input closed.\n");
                break;
            }

            // Skip empty lines
            if (strlen(input_buf) <= 1) {
                printf("%s", prompt);
                fflush(stdout);
                continue;
            }

            // HEREDOC PROCESSING (<< delimiter)
            if (strstr(input_buf, "<<") && !strstr(input_buf, "<<<")) {
                char delimiter[64] = {0};
                char *heredoc_pos = strstr(input_buf, "<<");
                sscanf(heredoc_pos + 2, "%s", delimiter); // extract delimiter

                // Truncate heredoc marker from command
                *heredoc_pos = '\0';
                input_buf[strcspn(input_buf, "\n")] = '\0'; // remove newline

                // Temporarily switch back to blocking mode for heredoc input
                int old_flags = fcntl(STDIN_FILENO, F_GETFL);
                fcntl(STDIN_FILENO, F_SETFL, old_flags & ~O_NONBLOCK);

                // Read heredoc content until the delimiter is typed
                char heredoc_data[4096] = "";
                char line[1024];
                while (1) {
                    printf("heredoc> ");
                    fflush(stdout);
                    if (!fgets(line, sizeof(line), stdin)) break;

                    // Strip newline and compare with delimiter
                    char temp[1024];
                    strcpy(temp, line);
                    temp[strcspn(temp, "\n")] = '\0';
                    if (strcmp(temp, delimiter) == 0)
                        break;

                    // Append the input line to heredoc_data, add escaped newline
                    strncat(heredoc_data, temp, sizeof(heredoc_data) - strlen(heredoc_data) - 1);
                    strncat(heredoc_data, "\\n", sizeof(heredoc_data) - strlen(heredoc_data) - 1);
                }

                // Rewrite input command as: printf "heredoc..." | original_command
                char result[1024];
                snprintf(result, sizeof(result), "printf %s | %s\n", heredoc_data, input_buf);
                strncpy(input_buf, result, sizeof(result) - 1);
                input_buf[sizeof(result) - 1] = '\0';

                // Restore original non-blocking mode
                fcntl(STDIN_FILENO, F_SETFL, old_flags);
            }

            // Send the command to the server
            if (write(sock, input_buf, strlen(input_buf)) < 0) {
                perror("[CLIENT] Failed to send command");
                break;
            }

            waiting_for_response = 1;
        }

        // SERVER RESPONSE HANDLING
        if (FD_ISSET(sock, &read_fds)) {
            int bytes;

            // Read all available data from the server
            while ((bytes = read(sock, buffer, sizeof(buffer) - 1)) > 0) {
                buffer[bytes] = '\0';

                // Handle special control messages from server
                if (strncmp(buffer, "[HALT]", 6) == 0) {
                    printf("[CLIENT] Server halted. Exiting.\n");
                    exit(0);
                }
                if (strncmp(buffer, "[QUIT]", 6) == 0) {
                    printf("[CLIENT] Quit command received. Disconnecting.\n");
                    exit(0);
                }
                if (strncmp(buffer, "[ABORT]", 7) == 0) {
                    printf("\n[CLIENT] Abort!\n");
                    exit(0);
                }

                // Handle output from the command, until [END] marker
                if (waiting_for_response) {
                    char *end_marker = strstr(buffer, "[END]");
                    if (end_marker) {
                        *end_marker = '\0'; // Cut off at [END] marker
                        printf("%s\n", buffer);
                        fflush(stdout);
                        waiting_for_response = 0;

                        // Show new prompt
                        get_prompt(prompt, sizeof(prompt));
                        printf("%s", prompt);
                        fflush(stdout);
                        break;
                    }

                    // If no [END], just print ongoing data
                    printf("%s", buffer);
                    fflush(stdout);
                }
            }

        }
    }
}



/* ==============================================================================================
 * UNIX Domain Socket Client
 * ==============================================================================================
 *
 * Connects to a local UNIX socket at the given path and starts the interaction loop.
 * If the connection fails, the client exits with an error.
 *
 * ==============================================================================================
 * ============================================================================================== */


void run_unix_client(char *socket_path) {
    int sock;  // Socket file descriptor
    struct sockaddr_un server_addr;  // UNIX socket address structure

    // Create a UNIX domain stream socket
    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("[CLIENT] UNIX socket creation failed");
        exit(1);
    }

    // Initialize server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    // Copy socket path (with length validation)
    strncpy(server_addr.sun_path, socket_path, sizeof(server_addr.sun_path) - 1);

    // Connect to the server
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("[CLIENT] UNIX connection failed");
        close(sock);
        exit(1);
    }

    printf("[CLIENT] Connected to UNIX socket: %s\n", socket_path);

    // Run the main client interaction loop
    main_connection_loop(sock);

    // Cleanup: close the socket
    close(sock);
}


/* ==============================================================================================
 * TCP Socket Client
 * ==============================================================================================
 *
 * Connects to a remote TCP socket on the given host and port, then starts the interaction loop.
 * If the connection fails, an error is displayed and the client exits.
 *
 * ==============================================================================================
 * ============================================================================================== */


void run_tcp_client(const char *host, int port) {
    int sock;  // Socket file descriptor
    struct sockaddr_in server_addr;  // Internet socket address structure

    // Create an IPv4 TCP socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("[CLIENT] TCP socket creation failed");
        exit(1);
    }

    // Initialize server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;  // IPv4 address family
    server_addr.sin_port = htons(port);  // Convert port to network byte order

    // Convert IP address from text to binary form
    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
        perror("[CLIENT] Invalid host IP address");
        close(sock);
        exit(1);
    }

    // Connect to the server
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("[CLIENT] TCP connection failed");
        close(sock);
        exit(1);
    }

    printf("[CLIENT] Connected to TCP %s:%d\n", host, port);

    // Run the main client interaction loop
    main_connection_loop(sock);

    // Cleanup: close the socket
    close(sock);
}
