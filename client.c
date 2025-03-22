#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>
#include <time.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>


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
    char prompt[256];
    char buffer[1024];

    while (1) {
        get_prompt(prompt, sizeof(prompt)); // Generate shell prompt
        printf("%s ", prompt);
        fflush(stdout);

        // Read command from user
        if (!fgets(buffer, sizeof(buffer), stdin)) {
            printf("\n[CLIENT] Input closed. Exiting.\n");
            break;
        }

        // Skip empty lines
        if (strlen(buffer) <= 1) continue;

        // Remove trailing newline
        buffer[strcspn(buffer, "\n")] = '\0';

        // Send command to server
        if (write(sock, buffer, strlen(buffer)) < 0) {
            perror("[CLIENT] Failed to send command");
            break;
        }

        // Receive response from server
        int bytes_read = read(sock, buffer, sizeof(buffer) - 1);
        if (bytes_read < 0) {
            perror("[CLIENT] Failed to read from server");
            break;
        } else if (bytes_read == 0) {
            printf("[CLIENT] Server closed the connection.\n");
            break;
        }

        buffer[bytes_read] = '\0';

        if (strncmp(buffer, "[QUIT]", 6) == 0) {
            printf("[CLIENT] Quit command received. Disconnecting.\n");
            break;
        }

        if (strncmp(buffer, "[HALT]", 6) == 0) {
            printf("[CLIENT] Server requested shutdown. Exiting.\n");
            break;
        }

        // Print server's response
        printf("%s\n", buffer);
    }
}


void run_unix_client(char *socket_path) {
    int sock;
    struct sockaddr_un server_addr;

    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("[CLIENT] UNIX socket creation failed");
        exit(1);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, socket_path, sizeof(server_addr.sun_path) - 1);

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("[CLIENT] UNIX connection failed");
        close(sock);
        exit(1);
    }

    printf("[CLIENT] Connected to UNIX socket: %s\n", socket_path);

    main_connection_loop(sock);

    close(sock);
}

void run_tcp_client(const char *host, int port) {
    int sock;
    struct sockaddr_in server_addr;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("[CLIENT] TCP socket creation failed");
        exit(1);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
        perror("[CLIENT] Invalid host IP address");
        close(sock);
        exit(1);
    }

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("[CLIENT] TCP connection failed");
        close(sock);
        exit(1);
    }

    printf("[CLIENT] Connected to TCP %s:%d\n", host, port);

    main_connection_loop(sock);

    close(sock);
}
