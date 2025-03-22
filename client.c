#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>
#include <time.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>

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



void run_client(char *socket_path) {
    int sock;
    struct sockaddr_un server_addr;
    char prompt[256];
    char buffer[1024];

    // Create UNIX domain socket
    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, socket_path, sizeof(server_addr.sun_path) - 1);

    // Connect to the server
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("[ERROR] Connection failed");
        exit(1);
    }

    printf("[INFO] Connected to server.\n");

    // Main input loop
    while (1) {
        get_prompt(prompt, sizeof(prompt)); // Get current prompt string
        printf("%s ", prompt);
        fflush(stdout);

        // Read input from user
        if (!fgets(buffer, sizeof(buffer), stdin)) {
            printf("\nExiting.\n");
            break;
        }

        if (strlen(buffer) == 1) continue; // Skip empty input (only newline)

        // Send command to server
        write(sock, buffer, strlen(buffer));

        // Read response from server
        int bytes_read = read(sock, buffer, sizeof(buffer) - 1);
        if (bytes_read < 0) {
            perror("[ERROR] Failed to read from server");
            break;
        } else if (bytes_read == 0) {
            printf("[ERROR] Connection to server was closed.\n");
            break;
        }

        if (strncmp(buffer, "[QUIT]", 6) == 0) {
            printf("[CLIENT] Quit command issued. Disconnecting.\n");
            break;
        }

        // Optional: if server returns "halt", exit immediately
        if (strncmp(buffer, "[HALT]", 6) == 0) {
            printf("[INFO] Server requested shutdown. Exiting.\n");
            break;
        }

        buffer[bytes_read] = '\0';

        // Print server response
        printf("%s\n", buffer);
    }

    // Close socket connection
    close(sock);
}
