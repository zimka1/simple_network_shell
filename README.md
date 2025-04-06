# Simple Network Shell (SNS)

An interactive shell that runs in both **client** and **server** modes over UNIX or TCP sockets.
Supports standard shell-like command execution with features such as **piping**, **I/O redirection**,
**script execution**, and internal management commands.

---

## 📖 Author

**Aliaksei Zimnitski**  
2nd Year, 4th Semester – Informatika  
Date: *01.04.2025*

---

## 🧠 Features

- Dual operation mode: **server** and **client**
- Socket support:
  - UNIX domain sockets (local IPC)
  - TCP/IP sockets (remote connections)
- Command features:
  - Chaining using `;`
  - Piping using `|`
  - Input/output redirection with `<`, `>`, `>>`
- Built-in internal commands:
  - `help` — display internal command help
  - `halt` — stop the server and disconnect all clients
  - `quit` — disconnect a single client
  - `stat` — list all active connections
  - `abort <id>` — forcibly disconnect a specific client
- **Script execution** support from file (non-interactive)
- **Cross-platform**: Works on **Linux** and **FreeBSD**
- Modular code structure with separated libraries (e.g. redirection)
- Descriptive **English documentation**

---

## 🚀 Usage

### 🛠 Compile

```bash
gcc -o shell main.c server.c client.c redirections.c
```

### 🟢 Run as Server (default)

```bash
./shell -s                       # Default mode
./shell -s -u /tmp/myshell.sock # UNIX socket
./shell -s -p 1234 -i 127.0.0.1 # TCP socket
```

### 🔵 Run as Client

```bash
./shell -c -u /tmp/myshell.sock           # Connect to UNIX socket
./shell -c -p 1234 -i 127.0.0.1           # Connect to TCP socket
./shell -c "ls -la | grep txt"            # One-shot command
```

### 📜 Run Script (Server Only)

```bash
./shell script.txt
```

### ❓ Help

```bash
./shell -h
```

---

## 🧩 Optional Features Implemented

| ID  | Feature                                                                 | Points |
|-----|-------------------------------------------------------------------------|--------|
| 1   | Non-interactive mode (script file execution)                           | 2      |
| 2   | Cross-platform support (Linux, FreeBSD)                                | 2      |
| 3   | Internal command `stat` for active connections                         | 3      |
| 4   | Internal command `abort <id>` to terminate specific connections        | 2      |
| 7   | IP address flag `-i` for TCP sockets                                   | 2      |
| 11  | Use of external library for redirection handling                       | 2      |
| 15  | Internal commands work as flags with `-c` (e.g., `-halt`, `-help`)     | 2      |
| 23  | Extensive English documentation and inline comments                    | 1      |
|     | **Total**                                                              | **16** |

---

## 📚 Internal Commands

- `help`   – Show internal help message
- `cd`     – Change working directory
- `halt`   – Stop the server and all clients
- `quit`   – Disconnect current client
- `stat`   – Show all active client connections
- `abort`  – Disconnect a specific client by ID

---

## 🔧 Future Enhancements

- Prompt customization and formatting via internal `prompt` command
- Idle timeout with `-t` flag or environment variable
- Logging support (`-l logfile.txt`)
- Daemon mode support (`-d`)
- Wildcard/globbing (`*`) in command arguments
- Load config from file with `-C config.cfg`

---
