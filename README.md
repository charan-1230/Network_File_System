# Network File System (NFS)

A robust, scalable distributed file system supporting file operations, directory management, streaming, and redundancy.

---

## Table of Contents

- [Introduction](#introduction)
- [Architecture](#architecture)
- [Key Features](#key-features)
- [Technologies Used](#technologies-used)
- [Project Structure](#project-structure)
- [Setup & Running](#setup--running)
- [Usage & Commands](#usage--commands)
- [Assumptions](#assumptions)
- [Limitations](#limitations)

---

## Introduction

This project implements a distributed file system (DFS) with three major components:

- **Clients:** User-facing applications that request file operations (read, write, delete, stream, etc.).
- **Naming Server (NM):** The central coordinator that manages metadata, directory structure, and orchestrates communication between clients and storage servers.
- **Storage Servers (SS):** Responsible for actual file storage, retrieval, and redundancy. Multiple SS can be added dynamically.

Clients interact with the system to perform essential file operations, including:

- **Write (Sync/Async):** Create or update files/folders, with support for asynchronous writes for large files.
- **Read:** Retrieve file contents.
- **Delete:** Remove files/folders.
- **Create:** Generate new files/folders.
- **List:** List files and folders in a directory.
- **Metadata:** Get file size, permissions, and timestamps.
- **Streaming:** Stream audio files directly from the NFS.

---

## Architecture

- **Naming Server (NM):**
  - Maintains a global directory of all files/folders and their locations.
  - Handles client requests, determines the correct SS, and provides connection details.
  - Manages registration and dynamic addition of Storage Servers.
  - Handles backup, redundancy, and failure detection.

- **Storage Servers (SS):**
  - Store and manage files/folders.
  - Register with the NM, providing IP, ports, and accessible paths.
  - Support file operations and interact directly with clients after NM mediation.
  - Support dynamic addition and removal.

- **Clients:**
  - Connect to NM for metadata and SS location.
  - Communicate directly with SS for file operations.
  - Support both synchronous and asynchronous writes.

---

## Key Features

- **Distributed Architecture:** Centralized metadata management with distributed storage.
- **Dynamic Scaling:** Add/remove Storage Servers at runtime.
- **File & Directory Operations:** `ls`, `write`, `cat`, `mkdir`, `touch`, `rmdir`, `rm`.
- **Asynchronous Write Support:** Optimizes client response time for large files.
- **Streaming:** Stream audio files using `ffplay`.
- **Backup & Redundancy:** Automatic replication of files/folders across multiple SS for fault tolerance.
- **Caching:** LRU caching in NM for fast path lookups.
- **Access Control:** Restricts operations to within server root directories.
- **Logging & Bookkeeping:** All operations, IPs, and ports are logged for traceability.
- **Error Codes:** Clear error codes for all failure scenarios.
- **Efficient Search:** Trie based path lookup in NM for fast response.
- **Concurrent Clients:** Supports multiple clients with proper locking and ACK mechanisms.
- **Failure Detection and Recovery:** NM detects SS failures and serves data from replicas.

---

## Technologies Used

- **C (POSIX Sockets, Threads):** Core networking and concurrency.
- **Makefile:** Build automation.
- **Linux/Unix:** POSIX-compliant systems.
- **ffplay:** Media streaming (must be installed).
- **Trie:** Efficient metadata management.
- **LRU Cache:** Fast repeated lookups.

---

## Project Structure

```
NetworkFIleSystem/
├── client.c             # Client logic
├── naming_server.c      # Naming Server logic
├── storage_server.c     # Storage Server logic
├── helper.c/.h          # Shared utilities and protocol helpers
├── lock.c/.h            # Locking mechanisms
├── tries.c/.h           # Trie data structure for path management
├── ErrorCodes.h         # Error code definitions
├── Makefile             # Build automation
├── log.txt              # Operation logs
├── README.md            # Project documentation
└── (media/test files)
```

---

## Setup & Running

### 1. Build Components

```bash
make all    # Builds all components: Naming Server, Storage Server, and Client
```

Or build individually:
```bash
make naming_server    # Build only Naming Server
make storage_server   # Build only Storage Server  
make client          # Build only Client
```

### 2. Start Servers

- **Naming Server:**  
  ```bash
  make ns
  ```
  The system will prompt you to enter a port number. Example:
  ```
  Enter port number for Naming Server:
  5000
  ```

- **Storage Server:**  
  ```bash
  make ss
  ```
  The system will prompt you to enter the root path. Example:
  ```
  Enter root path for Storage Server:
  /home/charan/nfs_storage
  ```
  > **Note:** Use absolute paths for the root directory.  
  > Run this command multiple times in different terminals for multiple Storage Servers.

### 3. Start Client

- **Client:**  
  ```bash
  make cl
  ```
  The system will prompt you to enter the Naming Server IP address. Example:
  ```
  Enter IP address of Naming Server:
  127.0.0.1
  ```

### 4. Alternative Manual Execution

You can also run the executables directly:

- **Naming Server:**
  ```bash
  ./naming_server <PORT>
  ```
  Example: `./naming_server 5000`

- **Storage Server:**
  ```bash
  ./storage_server <IP> <ROOT_PATH>
  ```
  Example: `./storage_server 127.0.0.1 /home/charan/nfs_storage`

- **Client:**
  ```bash
  ./client <NAMING_SERVER_IP>
  ```
  Example: `./client 127.0.0.1`

### 5. Clean Backups

To remove backup folders:
```bash
rm -rf ./backupfolderforss
```

### 6. Clean Build Files

To remove all executables and object files:
```bash
make clean
```

---

**Tips:**
- Always use absolute paths for Storage Server root directories to avoid confusion.
- Ensure the port you choose for the Naming Server is open and not in use.
- For distributed deployment across multiple machines, use the actual IP addresses instead of 127.0.0.1.
- If you encounter issues, check the logs in `log.txt` for troubleshooting.
- Each Storage Server should have a unique root path.

--- 

## Usage & Commands

- **File Operations:**  
  `ls`, `write <src> <dest>`, `cat <file>`
- **Directory Management:**  
  `mkdir <dir>`, `touch <file>`, `rmdir <dir>`, `rm <file>`
- **Streaming:**  
  `stream <file>` (requires `ffplay`)
- **Async Write:**  
  `write` command supports async mode with client-side ACKs.

**Command Flow Examples:**

- `READ <path>`: NM returns SS IP/port; client fetches file.
- `STREAM <path>`: NM returns SS IP/port; client streams audio.
- `CREATE <path> <name>`: NM validates, instructs SS, updates metadata.
- `COPY <source> <dest>`: NM orchestrates copy between SS, updates metadata.

---

## Assumptions

1. **Max Storage Servers:** Up to 10 (tested with 5).
2. **Max Clients:** Unlimited.
3. **Path Restriction:** All paths must be within the server’s root directory.
4. **Buffer Size:** Default 2048 bytes; max file size 4096 bytes (configurable in `helper.h`).
5. **Ports:** Assigned automatically for multiple instances.
6. **Unique Paths:** No two SS should share the same root folder.
7. **Backups:** Stored in `backupforss` (not accessible as normal paths).
8. **Root Folder Protection:** Root folders cannot be copied or removed.

---

## Limitations

- **Buffer Flow:** Minor buffer flow issues may occur but do not affect core functionality.
- **Root Folder:** Cannot copy/remove root folders for safety.
