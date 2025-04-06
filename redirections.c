/* ==============================================================================================
 * I/O Redirection Handlers (Defined in redirections.c)
 * ==============================================================================================
 *
 * These functions allow redirection of standard input and output streams
 * to and from files. They are essential for supporting the <, >, and >>
 * redirection operators in shell commands.
 *
 * Functions:
 *   - output_redirection(): Overwrites a file with stdout content
 *   - output_redirection_append(): Appends stdout content to a file
 *   - input_redirection(): Replaces stdin with content from a file
 *
 * These functions are called from child processes after forking and before exec().
 * ==============================================================================================
 */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

void output_redirection(char *filename) {
    // Open (or create) a file for writing
    // O_WRONLY  - open for writing only
    // O_CREAT   - create the file if it does not exist
    // O_TRUNC   - truncate the file if it already exists
    // 0644      - file permissions: owner can read/write, others can only read
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        perror("Error opening file");
        exit(1);
    }
    // Redirect standard output (stdout) to the file
    // Now, everything written to stdout will be saved in the file
    dup2(fd, STDOUT_FILENO);
    // Close the file descriptor as it is no longer needed
    close(fd);
}

void output_redirection_append(char *filename) {
    // Open file for appending (create if it doesn't exist)
    int fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd == -1) {
        perror("Error opening file for append");
        exit(1);
    }
    // Redirect standard output to the file in append mode
    dup2(fd, STDOUT_FILENO);
    close(fd);
}

void input_redirection(char *filename){
    // Open the file for reading
    // O_RDONLY - open for read-only access
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        perror("Error opening input file");
        exit(1);
    }
    // Redirect standard input (stdin) to read from the file
    // Now, everything that would be read from stdin will come from the file
    dup2(fd, STDIN_FILENO);
    // Close the file descriptor as it is no longer needed
    close(fd);
}