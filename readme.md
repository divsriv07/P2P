# Tracker Server

## Overview

The Tracker Server is a robust C++ application designed to manage users, groups, and file sharing through socket programming. It leverages multi-threading with `pthreads` to handle multiple client connections concurrently, ensuring efficient and scalable operations. The server facilitates user authentication, group management, file uploads, and sharing functionalities, making it ideal for applications requiring centralized tracking and coordination.

## Features

- **User Management**: Create, login, and logout users with secure password handling.
- **Group Management**: Create groups, join/leave groups, and manage group membership requests.
- **File Sharing**: Upload files, list available files in groups, and manage file sharing permissions.
- **Concurrency**: Handle multiple client connections simultaneously using multi-threading.
- **Graceful Shutdown**: Support for server shutdown commands and signal handling to ensure smooth termination.
- **Thread Safety**: Utilizes mutexes to protect shared resources and ensure data integrity.

## Dependencies

- **C++11 or later**
- **POSIX Threads (`pthread`)**
- **OpenSSL**: For SHA1 hashing.
- **Standard Libraries**: Includes headers like `<iostream>`, `<map>`, `<vector>`, etc.

Ensure that OpenSSL is installed on your system. On Ubuntu, you can install it using:

```bash
sudo apt-get update
sudo apt-get install libssl-dev
```

## Compilation

To compile the Tracker Server, use the following command:

```bash
g++ -std=c++11 -pthread -lssl -lcrypto tracker.cpp -o tracker
```

- `-std=c++11`: Specifies the C++ version.
- `-pthread`: Links the pthread library for multi-threading.
- `-lssl -lcrypto`: Links OpenSSL libraries for SHA1 hashing.

## Usage

Run the Tracker Server with the following command:

```bash
./tracker <tracker_info.txt> <tracker_no>
```

- `<tracker_info.txt>`: Path to the tracker information file (not utilized in the current implementation but can be used for future enhancements).
- `<tracker_no>`: Tracker number to determine the port (e.g., if `tracker_no` is `1`, the server listens on port `5001`).

**Example:**

```bash
./tracker tracker_info.txt 1
```

The server will start listening on `127.0.0.1:5001` and await client connections.

## Socket Programming Overview

The Tracker Server employs socket programming to establish and manage communication between clients and the server. Here's a breakdown of the socket-related functionalities:

1. **Socket Creation**: 
   - Uses `socket(AF_INET, SOCK_STREAM, 0)` to create a TCP socket.
   
2. **Binding**:
   - Binds the socket to a specific IP address (`127.0.0.1`) and port (`5000 + tracker_no`) using `bind()`.

3. **Listening**:
   - The server listens for incoming connections with `listen(socket_desc, 5)`, where `5` is the backlog size.

4. **Accepting Connections**:
   - Utilizes `accept()` in a loop to accept incoming client connections. Each accepted connection spawns a new thread to handle client-server communication.

5. **Client Communication**:
   - Each client thread uses `recv()` to receive commands from the client and `send()` to respond.
   - Commands are parsed and appropriate actions are taken based on the command type.

6. **Graceful Shutdown**:
   - The server listens for a `shutdown` command from the server console or handles `SIGINT` (Ctrl+C) to initiate a graceful shutdown.
   - During shutdown, it notifies all connected clients, closes their sockets, and terminates the server socket.

## Main Functions and Commands

The server supports a variety of commands, each handled by specific functions. Below is a summary of the primary commands and their functionalities:

### User Commands

- **create_user `<user_id>` `<password>`**
  - Creates a new user with the specified ID and password.
  
- **login `<user_id>` `<password>`**
  - Authenticates a user and logs them in.
  
- **logout**
  - Logs out the currently logged-in user.

### Group Commands

- **create_group `<group_id>`**
  - Creates a new group with the specified ID. The creator becomes the group owner.
  
- **join_group `<group_id>`**
  - Sends a join request to the specified group.
  
- **leave_group `<group_id>`**
  - Leaves the specified group. If the user is the owner, ownership is transferred or the group is deleted if no members remain.
  
- **list_requests `<group_id>`**
  - Lists all pending join requests for the specified group (owner only).
  
- **accept_request `<group_id>` `<user_id>`**
  - Accepts a join request from the specified user to the group (owner only).
  
- **list_groups**
  - Lists all available groups.

### File Commands

- **upload_file `<file_name>` `<file_size>` `<file_sha1>` `<group_id>` `<chunk_sha1_1>` ... `<chunk_sha1_n>`**
  - Uploads a file to the specified group, including chunk SHA1 hashes for verification.
  
- **list_files `<group_id>`**
  - Lists all files available in the specified group.
  
- **stop_share `<group_id>` `<file_name>`**
  - Stops sharing the specified file in the group.

### Session Commands

- **quit**
  - Disconnects the client from the server.

### Server Commands

- **shutdown**
  - Initiates a server shutdown, notifying all connected clients and terminating the server gracefully.

## Concurrency and Threading

The server employs multi-threading to handle multiple clients concurrently:

- **Client Handler Threads**:
  - Each client connection is managed by a dedicated thread (`clientHandler`) that processes incoming commands and sends responses.
  - Threads are detached using `pthread_detach` to allow independent execution without requiring explicit joins.
  
- **Server Command Handler Thread**:
  - A separate thread (`serverCommandHandler`) listens for server-side commands (e.g., `shutdown`) from the console.
  
- **Thread Safety**:
  - Mutexes (`users_mutex`, `groups_mutex`, `clients_mutex`) are used to protect shared resources such as user data, group data, and the list of connected clients.
  
## Graceful Shutdown

The server supports graceful termination through two mechanisms:

1. **Server Command (`shutdown`)**:
   - Typing `shutdown` in the server console triggers the shutdown process.
   - The server notifies all connected clients with a shutdown message, closes their sockets, and terminates the main server socket.

2. **Signal Handling (`SIGINT`)**:
   - Pressing `Ctrl+C` sends a `SIGINT` signal.
   - The `signalHandler` function catches the signal, initiates the shutdown process, and ensures all resources are cleaned up properly.

## Error Handling

The server includes comprehensive error handling to ensure stability:

- **Command Validation**:
  - Each command is validated for the correct number of arguments and appropriate permissions.
  
- **Socket Errors**:
  - Errors during socket operations (`send`, `recv`, `bind`, etc.) are captured and reported.
  
- **Resource Management**:
  - Proper allocation and deallocation of memory for client sockets and threads to prevent leaks.
  
- **User Feedback**:
  - Informative error messages are sent back to clients for invalid operations or failed commands.

## Conclusion

The Tracker Server is a feature-rich application that demonstrates effective use of socket programming, multi-threading, and system-level programming in C++. Its modular design and comprehensive command set make it a solid foundation for applications requiring centralized tracking and coordination of users, groups, and file sharing.

For any issues or contributions, feel free to open an issue or pull request.








# Client

## Overview

The Tracker Client is a C++ application designed to interact with the Tracker Server for managing user operations and file sharing within groups. Utilizing socket programming and multi-threading with POSIX threads (`pthreads`), the client facilitates communication with the server, enabling functionalities such as user authentication, file uploads, and group management. The client ensures secure and efficient interactions by incorporating OpenSSL's EVP interface for SHA1 hashing and robust error handling mechanisms.

## Features

- **User Operations**: Execute commands like `upload_file` and `quit` to manage files and disconnect from the server.
- **File Management**: Upload files with SHA1 hash verification to ensure data integrity.
- **Concurrency**: Handle server responses concurrently using separate threads for receiving messages.
- **Error Handling**: Comprehensive error messages and validations guide user interactions.
- **Secure Communication**: Utilizes OpenSSL for hashing to maintain secure file transfers.

## Dependencies

- **C++11 or later**
- **POSIX Threads (`pthread`)**
- **OpenSSL**: For SHA1 hashing using the EVP interface.
- **Standard Libraries**: Includes headers like `<iostream>`, `<cstring>`, `<cstdlib>`, etc.

Ensure that OpenSSL is installed on your system. On Ubuntu, you can install it using:

```bash
sudo apt-get update
sudo apt-get install libssl-dev
```

## Compilation

To compile the Tracker Client, use the following command:

```bash
clang++ -std=c++11 client.cpp -o c -L/opt/homebrew/opt/openssl@3/lib -I/opt/homebrew/opt/openssl@3/include -lssl -lcrypto -pthread
```

- `-std=c++11`: Specifies the C++ version.
- `-L/opt/homebrew/opt/openssl@3/lib`: Links the OpenSSL library path.
- `-I/opt/homebrew/opt/openssl@3/include`: Includes the OpenSSL headers.
- `-lssl -lcrypto`: Links OpenSSL libraries for SHA1 hashing.
- `-pthread`: Links the pthread library for multi-threading.

**Note**: Adjust the OpenSSL library and include paths (`-L` and `-I` flags) based on your system's configuration if they differ from the provided paths.

## Usage

Run the Tracker Client with the following command:

```bash
./c <server_ip>:<server_port> <tracker_info.txt>
```

- `<server_ip>:<server_port>`: Specifies the server's IP address and port in the format `IP:PORT` (e.g., `127.0.0.1:5001`).
- `<tracker_info.txt>`: Path to the tracker information file (not utilized in the current implementation but can be used for future enhancements).

**Example:**

```bash
./c 127.0.0.1:5001 tracker_info.txt
```

The client will attempt to connect to the Tracker Server at the specified IP address and port. Upon successful connection, users can enter commands to interact with the server.

## Socket Programming Overview

The Tracker Client employs socket programming to establish and manage communication with the Tracker Server. Here's a breakdown of the socket-related functionalities:

1. **Socket Creation**:
   - Uses `socket(AF_INET, SOCK_STREAM, 0)` to create a TCP socket for reliable communication.

2. **Connecting to Server**:
   - Utilizes `connect()` to establish a connection to the server using the specified IP address and port.

3. **Data Transmission**:
   - **Sending Data**: Employs `send()` to transmit user commands to the server.
   - **Receiving Data**: Utilizes a separate thread (`receiveHandler`) that continuously listens for server responses using `recv()`, ensuring that incoming messages are handled asynchronously without blocking the main command input loop.

4. **Graceful Termination**:
   - Implements a `quit` command that allows users to disconnect from the server gracefully.
   - Handles server-initiated disconnections by listening for shutdown messages and terminating the client accordingly.

## Function Descriptions

### 1. `alertPrompt`

**Purpose**:  
Handles error messages by outputting them to the standard error stream. It can utilize the `perror` function for system-level error messages or display custom error messages.

**Parameters**:
- `errorMsg`: The error message to be displayed.
- `usePerror`: A boolean flag indicating whether to use `perror` for detailed error messages.

### 2. `ArrayList` Class

**Purpose**:  
A template-based dynamic array implementation that provides functionalities similar to `std::vector`. It manages a resizable array of elements of a specified type, supporting operations like addition, removal, retrieval, and dynamic resizing.

**Key Methods**:
- `add(T element)`: Adds an element to the array, resizing if necessary.
- `get(int index)`: Retrieves an element at a specified index.
- `removeAt(int index)`: Removes an element at a specified index.
- `size()`: Returns the number of elements.
- `isEmpty()`: Checks if the array is empty.
- `operator[]`: Overloaded to provide array-like access to elements.

### 3. `File` Class

**Purpose**:  
Represents a file within a group, managing file metadata and sharing information.

**Attributes**:
- `file_name`: Name of the file.
- `file_size`: Size of the file in bytes.
- `sha1`: SHA1 hash of the entire file for integrity verification.
- `chunk_sha1s`: List of SHA1 hashes for each chunk of the file.
- `shared_by_users`: List of `user_id`s who are sharing the file.

**Key Methods**:
- `addChunkSHA1(const string& sha1_hash)`: Adds a chunk's SHA1 hash.
- `addUser(const string& user_id)`: Adds a user to the shared-by list.

### 4. `locate`, `substring`, `myAtoi`

**Purpose**:  
Custom utility functions for string operations and conversions:
- `locate`: Finds the position of a specified delimiter within a string.
- `substring`: Extracts a substring from a given string based on start position and length.
- `myAtoi`: Converts a string to an integer, returning `0` if the conversion fails.

### 5. `receiveHandler`

**Purpose**:  
Handles receiving messages from the server in a separate thread to ensure that incoming data does not block the main command input loop.

**Workflow**:
1. Continuously listens for messages from the server using `recv()`.
2. Displays received messages to the user.
3. Handles server disconnections gracefully by notifying the user and terminating the client if necessary.

### 6. `uploadFile`

**Purpose**:  
Manages the process of uploading a file to a specified group with enhanced error checks and SHA1 hash computations for data integrity.

**Workflow**:
1. **File Validation**:
   - Checks if the file exists and is accessible using `stat`.
   - Verifies read permissions.
   - Ensures the file size does not exceed a predefined maximum limit (e.g., 1GB).

2. **File Reading and Hashing**:
   - Opens the file in read-only mode.
   - Reads the file in chunks (e.g., 512KB) and computes SHA1 hashes for each chunk using OpenSSL's EVP interface.
   - Accumulates the chunk hashes in an `ArrayList`.
   - Computes the overall SHA1 hash for the entire file.

3. **Command Preparation**:
   - Extracts the file name from the provided file path.
   - Constructs the `upload_file` command with the file name, size, SHA1 hash, group ID, and chunk hashes.

4. **Data Transmission**:
   - Sends the constructed command to the Tracker Server using `send()`.

5. **Feedback**:
   - Provides feedback to the user regarding the success or failure of the upload request.

### 7. `main`

**Purpose**:  
Acts as the entry point of the Tracker Client. It initializes the client, establishes a connection to the server, sets up necessary threads, and manages user input for command execution.

**Workflow**:
1. **Argument Validation**:
   - Ensures that the correct number of command-line arguments are provided.
   - Parses the server IP and port from the input arguments.

2. **Socket Initialization**:
   - Creates a TCP socket using `socket()`.
   - Prepares the server address structure (`sockaddr_in`).

3. **Connecting to Server**:
   - Establishes a connection to the server using `connect()`.

4. **OpenSSL Initialization**:
   - Initializes OpenSSL algorithms for SHA1 hashing.

5. **Thread Creation**:
   - Spawns a separate thread (`receiveHandler`) to handle incoming messages from the server.

6. **Command Processing Loop**:
   - Continuously prompts the user for commands.
   - Parses user input and executes corresponding actions.
   - Specifically handles the `upload_file` command by invoking the `uploadFile` function.
   - Sends other commands directly to the server.
   - Terminates the loop upon receiving the `quit` command.

7. **Cleanup**:
   - Closes the client socket before terminating the application.

## Concurrency and Threading

The Tracker Client leverages multi-threading to handle server communications efficiently:

- **Receiving Server Responses**:
  - A separate thread (`receiveHandler`) is spawned upon successful connection to the server. This thread continuously listens for and processes messages from the server, ensuring that the client can receive asynchronous updates without interrupting the user's ability to input commands.

- **Main Thread Operations**:
  - The main thread remains dedicated to handling user input, processing commands, and sending them to the server. This separation of concerns ensures that receiving server messages does not block or interfere with the client's command processing workflow.

## Error Handling and Validation

Robust error handling mechanisms are integrated throughout the client to ensure stability and provide meaningful feedback to users:

- **Connection Errors**:
  - Validates server address and port formats.
  - Checks for successful socket creation and connection to the server.
  - Handles errors during data transmission (`send` and `recv`), providing appropriate error messages.

- **File Operations**:
  - Validates file existence, accessibility, and read permissions before attempting uploads.
  - Ensures that files do not exceed the maximum allowed size.
  - Handles errors during file reading and hashing processes, informing the user of any issues encountered.

- **Command Validation**:
  - Ensures that commands entered by the user follow the correct format and contain the necessary arguments.
  - Provides usage instructions for commands with incorrect formats.

- **Resource Management**:
  - Ensures that dynamically allocated memory (e.g., for command parsing) is properly freed to prevent memory leaks.
  - Closes file descriptors and sockets appropriately during normal operations and error conditions.

## Graceful Termination

The client supports graceful termination to ensure that resources are released properly and that the user is informed of the disconnection status:

- **Quit Command**:
  - When the user enters the `quit` command, the client sends the command to the server, closes the socket connection, and terminates the application gracefully.

- **Server-Initiated Disconnection**:
  - If the server sends a shutdown message or closes the connection, the `receiveHandler` thread detects the disconnection, informs the user, and terminates the client application.

## Conclusion

The Tracker Client is a feature-rich application that demonstrates effective use of socket programming, multi-threading, and system-level programming in C++. Its robust architecture ensures secure and efficient interactions with the Tracker Server, providing users with seamless functionalities for managing files and groups. Comprehensive error handling and graceful termination mechanisms further enhance the client's reliability and user experience.

