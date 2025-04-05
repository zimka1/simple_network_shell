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

void main_connection_loop(int sock) {
    char prompt[256];          // Command prompt
    char buffer[501];         // Buffer for incoming data
    char input_buf[1024];      // User input buffer
    int waiting_for_response = 0;  // Are we expecting a response?

    // Set non-blocking I/O
    fcntl(sock, F_SETFL, O_NONBLOCK);
    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);

    // Initial prompt
    get_prompt(prompt, sizeof(prompt));
    printf("%s", prompt);
    fflush(stdout);

    while (1) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        if (!waiting_for_response)
            FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(sock, &read_fds);

        int max_fd = sock > STDIN_FILENO ? sock : STDIN_FILENO;

        int ready = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (ready < 0) {
            perror("[CLIENT] select failed");
            break;
        }

        // Handle user input
        if (!waiting_for_response && FD_ISSET(STDIN_FILENO, &read_fds)) {
            if (!fgets(input_buf, sizeof(input_buf), stdin)) {
                printf("[CLIENT] Input closed.\n");
                break;
            }

            if (strlen(input_buf) <= 1) {
                printf("%s", prompt);
                fflush(stdout);
                continue;
            }

            if (write(sock, input_buf, strlen(input_buf)) < 0) {
                perror("[CLIENT] Failed to send command");
                break;
            }

            waiting_for_response = 1;
        }

        // Handle server response
        if (FD_ISSET(sock, &read_fds)) {
            int bytes;
            while ((bytes = read(sock, buffer, sizeof(buffer) - 1)) > 0) {
                buffer[bytes] = '\0';

                // Control messages — must match exactly
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

                // Expected response — when waiting
                if (waiting_for_response) {
                    // Process [END]
                    char *end_marker = strstr(buffer, "[END]");
                    if (end_marker) {
                        *end_marker = '\0';
                        printf("%s\n", buffer);
                        fflush(stdout);
                        waiting_for_response = 0;

                        get_prompt(prompt, sizeof(prompt));
                        printf("%s", prompt);
                        fflush(stdout);
                        break;
                    }

                    // Otherwise, part of ongoing response
                    printf("%s", buffer);
                    fflush(stdout);
                }
            }

        }
    }
}




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
