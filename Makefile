# Target program name
TARGET = shell

# Source files
SRCS = main.c server.c client.c redirections.c

# Header files
HEADERS = server.h client.h redirections.h

# Object files
OBJS = $(SRCS:.c=.o)

# Compiler
CC = gcc

# Compiler flags
CFLAGS = -Wall -g

# Rule for building the program
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

# Rule for compiling .c files into .o files
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $<

# Clean rule (remove compiled files)
clean:
	rm -f $(OBJS) $(TARGET)

# Debug build rule with debug flags
debug: CFLAGS += -g
debug: $(TARGET)
