// client.cpp

#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sstream>
#include <algorithm>
#include <iomanip>      
#include <openssl/evp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

using namespace std;

#define BUFFER_SIZE 4096
#define CHUNK_SIZE (512 * 1024) 

// --- Custom Functions ---
void alertPrompt(const string& errorMsg, bool usePerror = false);
int myAtoi(const string& s);
long myAtol(const string& s);
int locate(const string& str, char delimiter);
string substring(const string& str, int start, int length);

// --- Custom ArrayList Class ---
template <typename T>
class ArrayList {
private:
    T* array;
    int capacity;
    int count;

    void resize() {
        capacity *= 2;
        T* newArray = new T[capacity];
        for (int i = 0; i < count; i++) {
            newArray[i] = array[i];
        }
        delete[] array;
        array = newArray;
    }

public:
    // Default constructor
    ArrayList() : capacity(10), count(0) {
        array = new T[capacity];
    }

    // Destructor
    ~ArrayList() {
        delete[] array;
    }

    // Copy constructor
    ArrayList(const ArrayList<T>& other) : capacity(other.capacity), count(other.count) {
        array = new T[capacity];
        for (int i = 0; i < count; i++) {
            array[i] = other.array[i];
        }
    }

    // Copy assignment operator
    ArrayList<T>& operator=(const ArrayList<T>& other) {
        if (this == &other) return *this;

        delete[] array;

        capacity = other.capacity;
        count = other.count;
        array = new T[capacity];
        for (int i = 0; i < count; i++) {
            array[i] = other.array[i];
        }
        return *this;
    }

    // Move constructor
    ArrayList(ArrayList<T>&& other) noexcept : array(other.array), capacity(other.capacity), count(other.count) {
        other.array = nullptr;
        other.capacity = 0;
        other.count = 0;
    }

    // Move assignment operator
    ArrayList<T>& operator=(ArrayList<T>&& other) noexcept {
        if (this == &other) return *this;

        delete[] array;

        array = other.array;
        capacity = other.capacity;
        count = other.count;

        other.array = nullptr;
        other.capacity = 0;
        other.count = 0;

        return *this;
    }

    void add(const T& element) {
        if (count == capacity) {
            resize();
        }
        array[count++] = element;
    }

    T& get(int index) {
        if (index < 0 || index >= count) {
            alertPrompt("Index out of bounds", false);
            exit(EXIT_FAILURE);
        }
        return array[index];
    }

    const T& get(int index) const {
        if (index < 0 || index >= count) {
            alertPrompt("Index out of bounds", false);
            exit(EXIT_FAILURE);
        }
        return array[index];
    }

    void removeAt(int index) {
        if (index < 0 || index >= count) {
            alertPrompt("Index out of bounds", false);
            return;
        }
        for (int i = index; i < count - 1; i++) {
            array[i] = array[i + 1];
        }
        count--;
    }

    int size() const {
        return count;
    }

    bool isEmpty() const {
        return count == 0;
    }

    T& operator[](int index) {
        return get(index);
    }

    const T& operator[](int index) const {
        return get(index);
    }

    
    void sort(bool (*compare)(const T&, const T&)) {
        for (int i = 0; i < count - 1; ++i) {
            for (int j = 0; j < count - i - 1; ++j) {
                if (!compare(array[j], array[j + 1])) {
                    // Swap
                    T temp = array[j];
                    array[j] = array[j + 1];
                    array[j + 1] = temp;
                }
            }
        }
    }

    void clear() {
        count = 0;
    }
};

// --- Enums for Command Types ---
enum class CommandType {
    CREATE_USER,
    LOGIN,
    CREATE_GROUP,
    JOIN_GROUP,
    LEAVE_GROUP,
    LIST_GROUPS,
    LIST_REQUESTS,
    ACCEPT_REQUEST,
    LIST_FILES,
    UPLOAD_FILE,
    DOWNLOAD_FILE,
    LOGOUT,
    QUIT,
    SHUTDOWN,
    UNKNOWN
};

// Function to map string commands to CommandType enums
CommandType getCommandType(const string& command) {
    if (command == "create_user") return CommandType::CREATE_USER;
    if (command == "login") return CommandType::LOGIN;
    if (command == "create_group") return CommandType::CREATE_GROUP;
    if (command == "join_group") return CommandType::JOIN_GROUP;
    if (command == "leave_group") return CommandType::LEAVE_GROUP;
    if (command == "list_groups") return CommandType::LIST_GROUPS;
    if (command == "list_requests") return CommandType::LIST_REQUESTS;
    if (command == "accept_request") return CommandType::ACCEPT_REQUEST;
    if (command == "list_files") return CommandType::LIST_FILES;
    if (command == "upload_file") return CommandType::UPLOAD_FILE;
    if (command == "download_file") return CommandType::DOWNLOAD_FILE;
    if (command == "logout") return CommandType::LOGOUT;
    if (command == "quit") return CommandType::QUIT;
    if (command == "shutdown") return CommandType::SHUTDOWN;
    return CommandType::UNKNOWN;
}

// --- Structure Definitions ---
struct PeerInfo {
    string userId;
    string ip;
    int port;
};

struct ChunkInfo {
    int chunkIndex;
    int availability; // Number of peers who have this chunk
    ArrayList<PeerInfo> peersWithChunk;
    string expectedSha1; // Expected SHA1 hash of the chunk
};

struct OwnedFileInfo {
    string filePath;
    string fileSHA1;
    ArrayList<string> chunkSHA1s;
    int totalChunks;
};

// --- Global Variables ---
volatile bool clientRunning = true;
int clientListenPort = 0;

string downloadFileName;
string downloadFilePath;
long downloadFileSize;
int totalChunks;
string downloadFileSha1;
ArrayList<ChunkInfo> chunkInfoList;
map<int, string> chunkData; // Map from chunk index to data

// Map to store files owned by the client
map<string, OwnedFileInfo> ownedFilesInfo;

// Mutex for thread safety
pthread_mutex_t downloadMutex = PTHREAD_MUTEX_INITIALIZER;

// Tracker connection socket
int trackerSocket = -1;

// --- Signal Handling for Graceful Shutdown ---
void signalHandler(int signum) {
    cout << "\nInterrupt signal (" << signum << ") received. Shutting down gracefully..." << endl;
    clientRunning = false;

    // Close the tracker socket to unblock any recv/send operations
    if (trackerSocket != -1) {
        close(trackerSocket);
        trackerSocket = -1;
    }
}

// --- SHA1 Computation Functions ---
// Compute SHA1 hash using OpenSSL EVP
string computeSHA1(const char* data, size_t len) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hashLen;

    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if (mdctx == NULL) {
        alertPrompt("EVP_MD_CTX_new failed", false);
        exit(EXIT_FAILURE);
    }

    if (EVP_DigestInit_ex(mdctx, EVP_sha1(), NULL) != 1) {
        alertPrompt("EVP_DigestInit_ex failed", false);
        EVP_MD_CTX_free(mdctx);
        exit(EXIT_FAILURE);
    }

    if (EVP_DigestUpdate(mdctx, data, len) != 1) {
        alertPrompt("EVP_DigestUpdate failed", false);
        EVP_MD_CTX_free(mdctx);
        exit(EXIT_FAILURE);
    }

    if (EVP_DigestFinal_ex(mdctx, hash, &hashLen) != 1) {
        alertPrompt("EVP_DigestFinal_ex failed", false);
        EVP_MD_CTX_free(mdctx);
        exit(EXIT_FAILURE);
    }

    EVP_MD_CTX_free(mdctx);

    stringstream ss;
    ss << hex << setw(2) << setfill('0');
    for (unsigned int i = 0; i < hashLen; ++i) {
        ss << setw(2) << (static_cast<unsigned int>(hash[i]) & 0xFF);
    }

    return ss.str();
}

// Function to compute SHA1 hash of a file using system calls
string computeFileSHA1(const string& filename) {
    int fd = open(filename.c_str(), O_RDONLY);
    if (fd < 0) {
        alertPrompt("Cannot open file to read: " + filename, true);
        return "";
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hashLen;

    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if (mdctx == NULL) {
        alertPrompt("EVP_MD_CTX_new failed at 326", false);
        close(fd);
        exit(EXIT_FAILURE);
    }

    if (EVP_DigestInit_ex(mdctx, EVP_sha1(), NULL) != 1) {
        alertPrompt("EVP_DigestInit_ex failed at 332", false);
        EVP_MD_CTX_free(mdctx);
        close(fd);
        exit(EXIT_FAILURE);
    }

    char buffer[BUFFER_SIZE];
    ssize_t bytesReadFile;
    while ((bytesReadFile = read(fd, buffer, sizeof(buffer))) > 0) {
        if (EVP_DigestUpdate(mdctx, buffer, bytesReadFile) != 1) {
            alertPrompt("EVP_DigestUpdate failed for file at 342: " + filename, false);
            EVP_MD_CTX_free(mdctx);
            close(fd);
            exit(EXIT_FAILURE);
        }
    }
    if (bytesReadFile < 0) {
        alertPrompt("Failed to read file: " + filename, true);
        EVP_MD_CTX_free(mdctx);
        close(fd);
        exit(EXIT_FAILURE);
    }

    if (EVP_DigestFinal_ex(mdctx, hash, &hashLen) != 1) {
        alertPrompt("EVP_DigestFinal_ex failed for file: " + filename, false);
        EVP_MD_CTX_free(mdctx);
        close(fd);
        exit(EXIT_FAILURE);
    }

    EVP_MD_CTX_free(mdctx);
    close(fd);

    stringstream ss;
    ss << hex << setw(2) << setfill('0');
    for (unsigned int i = 0; i < hashLen; ++i) {
        ss << setw(2) << (static_cast<unsigned int>(hash[i]) & 0xFF);
    }

    return ss.str();
}

// --- Utility Functions ---
// Function to extract base filename
string getBaseName(const string& filePath) {
    int pos = filePath.find_last_of('/');
    if (pos == -1) return filePath;
    return filePath.substr(pos + 1);
}

// Function to ensure all bytes are sent
bool sendAll(int socket, const char* buffer, size_t length) {
    size_t totalSent = 0;
    while (totalSent < length) {
        ssize_t sent = send(socket, buffer + totalSent, length - totalSent, 0);
        if (sent <= 0) {
            return false;
        }
        totalSent += sent;
    }
    return true;
}

// --- Peer Server Function ---
// Function to handle incoming connections from peers requesting chunks
void* peerServer(void* arg) {
    int listenPort = *(int*)arg;
    delete (int*)arg;

    int serverSocket, clientSocket, c;
    sockaddr_in serverAddr, clientAddr;
    char buffer[BUFFER_SIZE];

    // Create socket
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        alertPrompt("Could not create peer server socket", true);
        pthread_exit(NULL);
    }

    // Prepare sockaddr_in structure
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY; // Listen on all interfaces
    serverAddr.sin_port = htons(listenPort);

    // Bind
    if (::bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        alertPrompt("Peer server bind failed", true);
        close(serverSocket);
        pthread_exit(NULL);
    }

    // Listen
    if (listen(serverSocket, 10) < 0) {
        alertPrompt("Peer server listen failed", true);
        close(serverSocket);
        pthread_exit(NULL);
    }

    cout << "Peer server listening on port " << listenPort << endl;

    c = sizeof(sockaddr_in);

    // Accept incoming connections
    while (clientRunning && (clientSocket = accept(serverSocket, (sockaddr*)&clientAddr, (socklen_t*)&c)) >= 0) {
        memset(buffer, 0, BUFFER_SIZE);
        int readSize = recv(clientSocket, buffer, BUFFER_SIZE - 1, 0);
        if (readSize > 0) {
            buffer[readSize] = '\0';
            string request(buffer);
            // Parse request
            istringstream iss(request);
            string command, fileName;
            int chunkIndex;
            iss >> command >> fileName >> chunkIndex;

            if (command == "get_chunk") {
                // Check if the client has the requested file
                if (ownedFilesInfo.find(fileName) == ownedFilesInfo.end()) {
                    string errorMsg = "Error: File not found.\n";
                    sendAll(clientSocket, errorMsg.c_str(), errorMsg.length());
                    close(clientSocket);
                    continue;
                }

                OwnedFileInfo fileInfo = ownedFilesInfo.at(fileName);

                // Get file size using stat
                struct stat st;
                if (stat(fileInfo.filePath.c_str(), &st) != 0) {
                    alertPrompt("Failed to get file size: " + fileInfo.filePath, true);
                    string errorMsg = "Error: Cannot get file size.\n";
                    sendAll(clientSocket, errorMsg.c_str(), errorMsg.length());
                    close(clientSocket);
                    continue;
                }
                off_t fileSize = st.st_size;

                // Calculate offset and expected chunk size
                off_t offset = static_cast<off_t>(chunkIndex) * CHUNK_SIZE;
                size_t expectedChunkSize = CHUNK_SIZE;
                if (chunkIndex == fileInfo.totalChunks - 1) {
                    expectedChunkSize = fileSize - (fileInfo.totalChunks - 1) * CHUNK_SIZE;
                }

                if (offset >= fileSize) {
                    string errorMsg = "Error: Invalid chunk index.\n";
                    sendAll(clientSocket, errorMsg.c_str(), errorMsg.length());
                    close(clientSocket);
                    continue;
                }

                // Open the file
                int fd = open(fileInfo.filePath.c_str(), O_RDONLY);
                if (fd < 0) {
                    alertPrompt("Failed to open file for chunk transfer: " + fileInfo.filePath, true);
                    string errorMsg = "Error: Cannot open file.\n";
                    sendAll(clientSocket, errorMsg.c_str(), errorMsg.length());
                    close(clientSocket);
                    continue;
                }

                // Seek to the chunk's position
                if (lseek(fd, offset, SEEK_SET) == (off_t)-1) {
                    alertPrompt("Failed to seek to chunk position", true);
                    string errorMsg = "Error: Cannot seek to chunk.\n";
                    sendAll(clientSocket, errorMsg.c_str(), errorMsg.length());
                    close(fd);
                    close(clientSocket);
                    continue;
                }

                // Read the chunk in a loop to ensure all data is read
                char* chunkBuffer = new char[expectedChunkSize];
                size_t totalBytesRead = 0;
                bool readError = false;
                while (totalBytesRead < expectedChunkSize) {
                    ssize_t bytesRead = read(fd, chunkBuffer + totalBytesRead, expectedChunkSize - totalBytesRead);
                    if (bytesRead < 0) {
                        alertPrompt("Failed to read chunk from file", true);
                        string errorMsg = "Error: Cannot read chunk.\n";
                        sendAll(clientSocket, errorMsg.c_str(), errorMsg.length());
                        readError = true;
                        break;
                    } else if (bytesRead == 0) {
                        // End of file reached unexpectedly
                        break;
                    }
                    totalBytesRead += bytesRead;
                }

                if (!readError) {
                    // Debugging statements
                    cout << "Peer Server: Serving chunk " << chunkIndex << " of file " << fileName << endl;
                    cout << "Chunk offset: " << offset << ", Expected chunk size: " << expectedChunkSize << endl;
                    cout << "Bytes read from file: " << totalBytesRead << endl;

                    // Send the chunk data
                    if (!sendAll(clientSocket, chunkBuffer, totalBytesRead)) {
                        alertPrompt("Failed to send chunk data to peer.", false);
                    }

                    cout << "Served chunk " << chunkIndex << " of file " << fileName << " to peer." << endl;
                }

                delete[] chunkBuffer;
                close(fd);
            } else {
                string errorMsg = "Error: Invalid command.\n";
                sendAll(clientSocket, errorMsg.c_str(), errorMsg.length());
            }

            close(clientSocket);
        }
    }

    close(serverSocket);
    pthread_exit(NULL);
}

// --- Download Chunk Function ---
void* downloadChunk(void* arg) {
    int listIndex = *(int*)arg;
    delete (int*)arg;

    // Get the chunk info
    pthread_mutex_lock(&downloadMutex);
    if (listIndex >= chunkInfoList.size()) {
        alertPrompt("List index out of range: " + to_string(listIndex), false);
        pthread_mutex_unlock(&downloadMutex);
        pthread_exit(NULL);
    }
    ChunkInfo chunkInfo = chunkInfoList.get(listIndex);
    pthread_mutex_unlock(&downloadMutex);

    int chunkIndex = chunkInfo.chunkIndex;

    // Calculate expected chunk size
    size_t expectedChunkSize = CHUNK_SIZE;
    if (chunkIndex == totalChunks - 1) {
        expectedChunkSize = downloadFileSize - (totalChunks - 1) * CHUNK_SIZE;
    }

    // Try to download from the peers who have this chunk
    bool success = false;
    for (int i = 0; i < chunkInfo.peersWithChunk.size(); ++i) {
        PeerInfo peer = chunkInfo.peersWithChunk.get(i);

        // Create socket
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            alertPrompt("Could not create socket to peer", true);
            continue;
        }

        sockaddr_in peerAddr;
        peerAddr.sin_family = AF_INET;
        peerAddr.sin_port = htons(peer.port);
        if (inet_pton(AF_INET, peer.ip.c_str(), &peerAddr.sin_addr) <= 0) {
            alertPrompt("Invalid peer IP address: " + peer.ip, false);
            close(sock);
            continue;
        }

        // Connect to peer
        if (connect(sock, (sockaddr*)&peerAddr, sizeof(peerAddr)) < 0) {
            alertPrompt("Could not connect to peer " + peer.userId, true);
            close(sock);
            continue;
        }

        // Send get_chunk command
        string getChunkCommand = "get_chunk " + downloadFileName + " " + to_string(chunkIndex) + "\n";
        if (!sendAll(sock, getChunkCommand.c_str(), getChunkCommand.length())) {
            alertPrompt("Failed to send get_chunk command to peer " + peer.userId, false);
            close(sock);
            continue;
        }

        // Receive chunk data
        char* chunkBuffer = new char[expectedChunkSize];
        size_t totalBytesReceived = 0;
        bool errorOccurred = false;

        while (totalBytesReceived < expectedChunkSize) {
            ssize_t bytesReceived = recv(sock, chunkBuffer + totalBytesReceived, expectedChunkSize - totalBytesReceived, 0);
            if (bytesReceived < 0) {
                alertPrompt("Failed to receive chunk from peer " + peer.userId, true);
                errorOccurred = true;
                break;
            } else if (bytesReceived == 0) {
                alertPrompt("Connection closed by peer " + peer.userId + " before receiving full chunk.", false);
                errorOccurred = true;
                break;
            }
            totalBytesReceived += bytesReceived;
        }

        // Debugging statements
        cout << "Downloading chunk " << chunkIndex << " from peer " << peer.userId << endl;
        cout << "Expected chunk size: " << expectedChunkSize << ", Total bytes received: " << totalBytesReceived << endl;

        if (errorOccurred || totalBytesReceived != expectedChunkSize) {
            cout << "Warning: Expected " << expectedChunkSize << " bytes, but received " << totalBytesReceived << " bytes." << endl;
            delete[] chunkBuffer;
            close(sock);
            continue;
        }

        // Compute SHA1 of received chunk
        string receivedChunkSha1 = computeSHA1(chunkBuffer, totalBytesReceived);

        cout << "Computed SHA1 of received chunk: " << receivedChunkSha1 << endl;
        cout << "Expected SHA1 of chunk: " << chunkInfo.expectedSha1 << endl;

        if (receivedChunkSha1 != chunkInfo.expectedSha1) {
            alertPrompt("SHA1 mismatch for chunk " + to_string(chunkIndex) + " from peer " + peer.userId, false);
            delete[] chunkBuffer;
            close(sock);
            continue;
        }

        
        pthread_mutex_lock(&downloadMutex);
        chunkData[chunkIndex] = string(chunkBuffer, totalBytesReceived);
        pthread_mutex_unlock(&downloadMutex);

        cout << "Successfully downloaded chunk " << chunkIndex << " from peer " << peer.userId << endl;

        delete[] chunkBuffer;
        close(sock);
        success = true;
        break; // Break after successful download
    }

    if (!success) {
        alertPrompt("Failed to download chunk " + to_string(chunkIndex), false);
    }

    pthread_exit(NULL);
}

// --- Tracker Communication Function ---
void* trackerCommunication(void* arg) {
    char buffer[BUFFER_SIZE];
    int readSize;

    while (clientRunning) {
        cout << ">> ";
        cout.flush(); 
        string command;
        if (!getline(cin, command)) {
            // EOF detected (e.g., Ctrl+D)
            clientRunning = false;
            break;
        }

        if (command.empty()) continue;

        
        ArrayList<string> tokens;       // Split the command into tokens
        char* commandCStr = new char[command.length() + 1];
        strcpy(commandCStr, command.c_str());
        char* tokenPtr = strtok(commandCStr, " \n");
        while (tokenPtr != NULL) {
            tokens.add(string(tokenPtr));
            tokenPtr = strtok(NULL, " \n");
        }
        delete[] commandCStr;

        if (tokens.size() == 0) continue;

        string cmd = tokens.get(0);
        CommandType commandType = getCommandType(cmd);

        switch (commandType) {
            case CommandType::LOGIN: {
                if (tokens.size() != 3) {
                    cout << "Usage: login <user_id> <password>" << endl;
                    continue;
                }

                string userId = tokens.get(1);
                string password = tokens.get(2);

                // Prepare login command with IP and port
                string loginCommand = "login " + userId + " " + password + " " + "127.0.0.1" + " " + to_string(clientListenPort) + "\n";

                // Send login command to tracker
                if (!sendAll(trackerSocket, loginCommand.c_str(), loginCommand.length())) {
                    alertPrompt("Failed to send login command to tracker.", false);
                    continue;
                }

                // Receive response
                readSize = recv(trackerSocket, buffer, BUFFER_SIZE - 1, 0);
                if (readSize > 0) {
                    buffer[readSize] = '\0';
                    cout << buffer;
                } else if (readSize == 0) {
                    alertPrompt("Tracker closed the connection.", false);
                    clientRunning = false;
                    break;
                } else {
                    alertPrompt("recv failed", true);
                    clientRunning = false;
                    break;
                }
                break;
            }
            case CommandType::UPLOAD_FILE: {
                // Expected format: upload_file <file_path> <group_id>
                if (tokens.size() != 3) {
                    cout << "Usage: upload_file <file_path> <group_id>" << endl;
                    continue;
                }

                string filePath = tokens.get(1);
                string groupId = tokens.get(2);

                // Check if the file exists using system calls
                struct stat st;
                if (stat(filePath.c_str(), &st) != 0) {
                    alertPrompt("File does not exist: " + filePath, true);
                    continue;
                }

                long fileSize = st.st_size;

                // Compute file SHA1
                string fileSha1 = computeFileSHA1(filePath);
                if (fileSha1.empty()) {
                    alertPrompt("Failed to compute SHA1 of the file.", false);
                    continue;
                }

                // Split the file into chunks and compute chunk SHA1s
                ArrayList<string> chunkSha1s;
                int totalChunksLocal = 0;

                int fd = open(filePath.c_str(), O_RDONLY);
                if (fd < 0) {
                    alertPrompt("Failed to open file for reading: " + filePath, true);
                    continue;
                }

                char* chunkBuffer = new char[CHUNK_SIZE];
                ssize_t bytesRead;
                while ((bytesRead = read(fd, chunkBuffer, CHUNK_SIZE)) > 0) {
                    string chunkSha1 = computeSHA1(chunkBuffer, bytesRead);
                    chunkSha1s.add(chunkSha1);
                    totalChunksLocal++;
                }
                if (bytesRead < 0) {
                    alertPrompt("Failed to read file: " + filePath, true);
                    delete[] chunkBuffer;
                    close(fd);
                    continue;
                }
                delete[] chunkBuffer;
                close(fd);

                // Prepare upload_file command
                string uploadCommand = "upload_file " + getBaseName(filePath) + " " + to_string(fileSize) + " " + fileSha1 + " " + groupId;
                for (int i = 0; i < chunkSha1s.size(); ++i) {
                    uploadCommand += " " + chunkSha1s.get(i);
                }
                uploadCommand += "\n"; // Append newline

                // Send upload_file command to tracker
                if (!sendAll(trackerSocket, uploadCommand.c_str(), uploadCommand.length())) {
                    alertPrompt("Failed to send upload_file command to tracker.", false);
                    continue;
                }

                // Receive response
                readSize = recv(trackerSocket, buffer, BUFFER_SIZE - 1, 0);
                if (readSize > 0) {
                    buffer[readSize] = '\0';
                    cout << buffer;

                    // Optionally, add the file to ownedFilesInfo if upload is successful
                    if (strstr(buffer, "success") != NULL || strstr(buffer, "created") != NULL || strstr(buffer, "File already exists. Added you as a sharer.") != NULL) {
                        OwnedFileInfo ownedFile;
                        ownedFile.filePath = filePath;
                        ownedFile.fileSHA1 = fileSha1;
                        ownedFile.chunkSHA1s = chunkSha1s;
                        ownedFile.totalChunks = totalChunksLocal;
                        ownedFilesInfo[getBaseName(filePath)] = ownedFile;
                    }
                } else if (readSize == 0) {
                    alertPrompt("Tracker closed the connection.", false);
                    clientRunning = false;
                    break;
                } else {
                    alertPrompt("recv failed", true);
                    clientRunning = false;
                    break;
                }
                break;
            }
            case CommandType::DOWNLOAD_FILE: {
                // Expected format: download_file <group_id> <file_name> <destination_path>
                if (tokens.size() != 4) {
                    cout << "Usage: download_file <group_id> <file_name> <destination_path>" << endl;
                    continue;
                }

                string groupId = tokens.get(1);
                string fileName = tokens.get(2);
                string destinationPath = tokens.get(3);
                downloadFileName = fileName;

                // Prepare download_file command
                string downloadCommand = "download_file " + groupId + " " + fileName + "\n";

                // Send download_file command to tracker
                if (!sendAll(trackerSocket, downloadCommand.c_str(), downloadCommand.length())) {
                    alertPrompt("Failed to send download_file command to tracker.", false);
                    continue;
                }

                // Receive response from tracker
                readSize = recv(trackerSocket, buffer, BUFFER_SIZE - 1, 0);
                if (readSize > 0) {
                    buffer[readSize] = '\0';
                    string responseStr(buffer);
                    cout << responseStr;

                    if (responseStr.find("Error:") == 0) {
                        continue;
                    }

                    // Parse the download_info response
                    istringstream responseStream(responseStr);
                    string infoTag;
                    responseStream >> infoTag;
                    if (infoTag != "download_info") {
                        alertPrompt("Invalid response from tracker.", false);
                        continue;
                    }

                    // Extract file metadata
                    responseStream >> downloadFileSize >> totalChunks;
                    int chunkSize;
                    responseStream >> chunkSize;
                    responseStream >> downloadFileSha1;

                    // Extract chunk availability and peer info
                    chunkInfoList.clear();
                    chunkData.clear();
                    for (int i = 0; i < totalChunks; ++i) {
                        ChunkInfo chunk;
                        responseStream >> chunk.chunkIndex >> chunk.availability >> chunk.expectedSha1;
                        for (int j = 0; j < chunk.availability; ++j) {
                            PeerInfo peer;
                            responseStream >> peer.userId >> peer.ip >> peer.port;
                            chunk.peersWithChunk.add(peer);
                        }
                        chunkInfoList.add(chunk);
                    }

                    // Implement the rarest first strategy by sorting the chunkInfoList
                    chunkInfoList.sort([](const ChunkInfo& a, const ChunkInfo& b) -> bool {
                        return a.availability < b.availability;
                    });

                    // Start downloading chunks using threads
                    ArrayList<pthread_t> threads;
                    for (int i = 0; i < chunkInfoList.size(); ++i) {
                        pthread_t tid;
                        int* arg = new int;
                        *arg = i; // Index into chunkInfoList
                        if (pthread_create(&tid, NULL, downloadChunk, arg) != 0) {
                            alertPrompt("Failed to create thread for chunk " + to_string(chunkInfoList.get(i).chunkIndex), false);
                            delete arg;
                            continue;
                        }
                        threads.add(tid);
                    }

                    
                    for (int i = 0; i < threads.size(); ++i) {
                        pthread_join(threads.get(i), NULL);
                    }

                    
                    downloadFilePath = destinationPath + "/" + fileName;
                    int outfile_fd = open(downloadFilePath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
                    if (outfile_fd < 0) {
                        alertPrompt("Could not create output file: " + downloadFilePath, true);
                        continue;
                    }

                    for (int i = 0; i < totalChunks; ++i) {
                        pthread_mutex_lock(&downloadMutex);
                        auto it = chunkData.find(i);
                        pthread_mutex_unlock(&downloadMutex);
                        if (it != chunkData.end()) {
                            const string& data = it->second;
                            if (write(outfile_fd, data.c_str(), data.length()) < 0) {
                                alertPrompt("Failed to write to output file: " + downloadFilePath, true);
                                break;
                            }
                        } else {
                            alertPrompt("Missing chunk " + to_string(i), false);
                        }
                    }
                    close(outfile_fd);

                    
                    string downloadedFileSha1 = computeFileSHA1(downloadFilePath);          // Verify the downloaded file
                    if (downloadedFileSha1 == downloadFileSha1) {
                        cout << "File downloaded and verified successfully." << endl;
                    } else {
                        alertPrompt("File verification failed for " + downloadFilePath, false);
                    }
                } else if (readSize == 0) {
                    alertPrompt("Tracker closed the connection.", false);
                    clientRunning = false;
                    break;
                } else {
                    alertPrompt("recv failed", true);
                    clientRunning = false;
                    break;
                }
                break;
            }
            case CommandType::QUIT: {
                string quitCommand = "quit\n";
                if (!sendAll(trackerSocket, quitCommand.c_str(), quitCommand.length())) {
                    alertPrompt("Failed to send quit command to tracker.", false);
                }
                clientRunning = false;
                break;
            }
            case CommandType::SHUTDOWN: {
                
                cout << "*** Tracker is shutting down. Disconnecting... ***" << endl;
                clientRunning = false;
                break;
            }
            default: {
                
                string commandToSend = command + "\n";
                if (!sendAll(trackerSocket, commandToSend.c_str(), commandToSend.length())) {
                    alertPrompt("Failed to send command to tracker.", false);
                    continue;
                }

                // Receive response
                readSize = recv(trackerSocket, buffer, BUFFER_SIZE - 1, 0);
                if (readSize > 0) {
                    buffer[readSize] = '\0';
                    cout << buffer;
                } else if (readSize == 0) {
                    alertPrompt("Tracker closed the connection.", false);
                    clientRunning = false;
                    break;
                } else {
                    alertPrompt("recv failed", true);
                    clientRunning = false;
                    break;
                }
                break;
            }
        }
    }

    pthread_exit(NULL);
}

// --- Main Function ---
int main(int argc, char* argv[]) {
    
    signal(SIGINT, signalHandler);

    // Initialize OpenSSL
    OpenSSL_add_all_digests();

    if (argc != 3) {
        alertPrompt("Usage: " + string(argv[0]) + " <clientIp:clientPort> <tracker_info.txt>", false);
        exit(EXIT_FAILURE);
    }

    string clientIpPort = argv[1];
    string trackerInfoFile = argv[2];

    
    int colonPos = locate(clientIpPort, ':');
    if (colonPos == -1) {
        alertPrompt("Invalid client IP:PORT format.", false);
        exit(EXIT_FAILURE);
    }
    string clientIp = substring(clientIpPort, 0, colonPos);
    string portStr = substring(clientIpPort, colonPos + 1, clientIpPort.length() - colonPos - 1);
    clientListenPort = myAtoi(portStr);

    // Read tracker IP and port from tracker_info.txt using system calls
    int trackerInfoFd = open(trackerInfoFile.c_str(), O_RDONLY);
    if (trackerInfoFd < 0) {
        alertPrompt("Could not open tracker info file", true);
        exit(EXIT_FAILURE);
    }

    char trackerInfoBuffer[BUFFER_SIZE];
    ssize_t bytesReadFile = read(trackerInfoFd, trackerInfoBuffer, BUFFER_SIZE - 1);
    if (bytesReadFile <= 0) {
        alertPrompt("Could not read tracker info file", true);
        close(trackerInfoFd);
        exit(EXIT_FAILURE);
    }
    close(trackerInfoFd);
    trackerInfoBuffer[bytesReadFile] = '\0';

   
    istringstream trackerInfoStream(trackerInfoBuffer);
    string trackerIp;
    string trackerPortStr;
    int trackerPort;
    try {
        trackerInfoStream >> trackerIp >> trackerPortStr;
        trackerPort = myAtoi(trackerPortStr);
    } catch (...) {
        alertPrompt("Invalid format in tracker_info.txt", false);
        exit(EXIT_FAILURE);
    }

    // Connect to tracker
    trackerSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (trackerSocket < 0) {
        alertPrompt("Could not create socket to connect to tracker", true);
        exit(EXIT_FAILURE);
    }

    sockaddr_in trackerAddr;
    trackerAddr.sin_family = AF_INET;
    trackerAddr.sin_port = htons(trackerPort);
    if (inet_pton(AF_INET, trackerIp.c_str(), &trackerAddr.sin_addr) <= 0) {
        alertPrompt("Invalid tracker IP address.", false);
        close(trackerSocket);
        exit(EXIT_FAILURE);
    }

    if (connect(trackerSocket, (sockaddr*)&trackerAddr, sizeof(trackerAddr)) < 0) {
        alertPrompt("Could not connect to tracker", true);
        close(trackerSocket);
        exit(EXIT_FAILURE);
    }

    cout << "Connected to tracker at " << trackerIp << ":" << trackerPort << endl;

    // Start peer server thread
    pthread_t peerServerThread;
    int* peerPortArg = new int(clientListenPort);
    if (pthread_create(&peerServerThread, NULL, peerServer, peerPortArg) != 0) {
        alertPrompt("Could not create peer server thread.", false);
        close(trackerSocket);
        exit(EXIT_FAILURE);
    }

    
    pthread_t trackerCommThread;
    if (pthread_create(&trackerCommThread, NULL, trackerCommunication, NULL) != 0) {
        alertPrompt("Could not create tracker communication thread.", false);
        clientRunning = false;
        close(trackerSocket);
        pthread_cancel(peerServerThread);
        pthread_join(peerServerThread, NULL);
        exit(EXIT_FAILURE);
    }

    
    pthread_join(trackerCommThread, NULL);

    
    clientRunning = false;
    close(trackerSocket);
    pthread_cancel(peerServerThread);
    pthread_join(peerServerThread, NULL);

   
    EVP_cleanup();

    cout << "Client terminated." << endl;

    return 0;
}

// --- Implementations of Custom Functions ---
void alertPrompt(const string& errorMsg, bool usePerror) {
    if (usePerror) {
        perror(errorMsg.c_str());
    } else {
        cerr << "Error: " << errorMsg << endl;
    }
}

int myAtoi(const string& s) {
    try {
        return stoi(s);
    } catch (...) {
        return 0;
    }
}

long myAtol(const string& s) {
    try {
        return stol(s);
    } catch (...) {
        return 0;
    }
}

int locate(const string& str, char delimiter) {
    size_t pos = str.find(delimiter);
    return pos == string::npos ? -1 : pos;
}

string substring(const string& str, int start, int length) {
    if (start < 0 || start >= (int)str.length()) return "";
    return str.substr(start, length);
}



// /Users/divsriv/Desktop/photo1.png
// clang++ -std=c++11 client.cpp -o c -L/opt/homebrew/opt/openssl@3/lib -I/opt/homebrew/opt/openssl@3/include -lssl -lcrypto -pthread