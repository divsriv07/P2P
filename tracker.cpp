// tracker.cpp

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
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

using namespace std;

#define BUFFER_SIZE 1024
#define CHUNK_SIZE (512 * 1024) // 512KB

// Enums for Command Types
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
    SHUTDOWN,
    QUIT,
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
    if (command == "shutdown") return CommandType::SHUTDOWN;
    if (command == "quit") return CommandType::QUIT;
    return CommandType::UNKNOWN;
}

// Custom ArrayList Template Class
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
            cerr << "Index out of bounds" << endl;
            exit(EXIT_FAILURE);
        }
        return array[index];
    }

    const T& get(int index) const {
        if (index < 0 || index >= count) {
            cerr << "Index out of bounds" << endl;
            exit(EXIT_FAILURE);
        }
        return array[index];
    }

    void removeAt(int index) {
        if (index < 0 || index >= count) {
            cerr << "Index out of bounds" << endl;
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

    // Custom sort function using a comparator
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

// UserInfo Class
class UserInfo {
public:
    string userId;
    string password;
    bool isLoggedIn;
    string ip;
    int port;

    UserInfo(const string& id, const string& pass)
        : userId(id), password(pass), isLoggedIn(false), ip(""), port(0) {}

    void setLoginStatus(bool status) {
        isLoggedIn = status;
    }
};

// Group Class
class Group {
public:
    string groupId;
    string ownerId;
    ArrayList<string> members;
    ArrayList<string> pendingRequests;

    Group(const string& id, const string& owner)
        : groupId(id), ownerId(owner) {
        members.add(owner);
    }
};

// File Class
class File {
public:
    string fileName;
    string fileSize;
    string fileSha1;
    ArrayList<string> chunkSha1s;
    map<string, ArrayList<int>> userChunks; // userId -> list of chunk indices

    // Default constructor
    File() {}

    // Parameterized constructor
    File(const string& name, const string& size, const string& sha1, const ArrayList<string>& chunks)
        : fileName(name), fileSize(size), fileSha1(sha1), chunkSha1s(chunks) {}
};

// Global Variables
map<string, UserInfo*> users;                 // userId to UserInfo*
map<string, Group*> groups;                   // groupId to Group*
map<int, string> clientUserMap;               // client socket to userId
map<string, ArrayList<File>> groupFiles;      // groupId -> List of Files

// Map of userId to their IP and port
map<string, pair<string, int>> userIpPortMap; // userId -> (IP, port)

// Mutexes for thread safety
pthread_mutex_t usersMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t groupsMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t clientsMutex = PTHREAD_MUTEX_INITIALIZER;

// List of all connected clients
ArrayList<int> connectedClients;

// Client ID counter
int clientIDCounter = 1;
map<int, int> clientSockToID; // Map socket descriptor to client ID

// Global flag for server running state
volatile bool serverRunning = true;

// Socket descriptor
int socketDesc = -1;

// Function Declarations
void alertPrompt(const string& errorMsg, bool usePerror = false);
int myAtoi(const string& s);
void* clientHandler(void* socketDescPtr);
void* serverCommandHandler(void* arg);
void signalHandler(int signum);

// Command Handlers
void handleCreateUser(const ArrayList<string>& tokens, int clientSock, string& response);
void handleLogin(const ArrayList<string>& tokens, int clientSock, string& response);
void handleCreateGroup(const ArrayList<string>& tokens, int clientSock, string& response);
void handleJoinGroup(const ArrayList<string>& tokens, int clientSock, string& response);
void handleLeaveGroup(const ArrayList<string>& tokens, int clientSock, string& response);
void handleListGroups(const ArrayList<string>& tokens, int clientSock, string& response);
void handleListRequests(const ArrayList<string>& tokens, int clientSock, string& response);
void handleAcceptRequest(const ArrayList<string>& tokens, int clientSock, string& response);
void handleListFiles(const ArrayList<string>& tokens, int clientSock, string& response);
void handleUploadFile(const ArrayList<string>& tokens, int clientSock, string& response);
void handleDownloadFile(const ArrayList<string>& tokens, int clientSock, string& response);
void handleShutdown(const ArrayList<string>& tokens, int clientSock, string& response);


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


void signalHandler(int signum) {
    cout << "\nInterrupt signal (" << signum << ") received. Shutting down tracker..." << endl;
    serverRunning = false;

    
    if (socketDesc != -1) {
        close(socketDesc);
        socketDesc = -1;
    }
}


bool handleCommand(const ArrayList<string>& tokens, int clientSock, string& response) {
    if (tokens.size() == 0) {
        response = "Invalid command.";
        return true;
    }

    string commandStr = tokens.get(0);
    CommandType cmdType = getCommandType(commandStr);

    switch(cmdType) {
        case CommandType::CREATE_USER:
            handleCreateUser(tokens, clientSock, response);
            break;
        case CommandType::LOGIN:
            handleLogin(tokens, clientSock, response);
            break;
        case CommandType::CREATE_GROUP:
            handleCreateGroup(tokens, clientSock, response);
            break;
        case CommandType::JOIN_GROUP:
            handleJoinGroup(tokens, clientSock, response);
            break;
        case CommandType::LEAVE_GROUP:
            handleLeaveGroup(tokens, clientSock, response);
            break;
        case CommandType::LIST_GROUPS:
            handleListGroups(tokens, clientSock, response);
            break;
        case CommandType::LIST_REQUESTS:
            handleListRequests(tokens, clientSock, response);
            break;
        case CommandType::ACCEPT_REQUEST:
            handleAcceptRequest(tokens, clientSock, response);
            break;
        case CommandType::LIST_FILES:
            handleListFiles(tokens, clientSock, response);
            break;
        case CommandType::UPLOAD_FILE:
            handleUploadFile(tokens, clientSock, response);
            break;
        case CommandType::DOWNLOAD_FILE:
            handleDownloadFile(tokens, clientSock, response);
            break;
        case CommandType::SHUTDOWN:
            handleShutdown(tokens, clientSock, response);
            break;
        case CommandType::QUIT:
            response = "Goodbye!";
            return false; // Indicate that the client should disconnect
        default:
            response = "Invalid command.";
            break;
    }

    return true;
}

// Implementations of Command Handlers

void handleCreateUser(const ArrayList<string>& tokens, int clientSock, string& response) {
    if (tokens.size() != 3) {
        response = "Usage: create_user <user_id> <password>";
        return;
    }

    string userId = tokens.get(1);
    string password = tokens.get(2);

    pthread_mutex_lock(&usersMutex);
    if (users.find(userId) != users.end()) {
        response = "Error: User already exists.";
    } else {
        UserInfo* newUser = new UserInfo(userId, password);
        users[userId] = newUser;
        response = "User created successfully.";
    }
    pthread_mutex_unlock(&usersMutex);
}

void handleLogin(const ArrayList<string>& tokens, int clientSock, string& response) {
    if (tokens.size() != 5) {
        response = "Usage: login <user_id> <password> <ip> <port>";
        return;
    }

    string userId = tokens.get(1);
    string password = tokens.get(2);
    string ip = tokens.get(3);
    int port = myAtoi(tokens.get(4));

    pthread_mutex_lock(&usersMutex);
    if (users.find(userId) == users.end()) {
        response = "Error: User does not exist.";
    }
    else if (users[userId]->password != password) {
        response = "Error: Incorrect password.";
    }
    else if (users[userId]->isLoggedIn) {
        response = "Error: User already logged in.";
    }
    else {
        users[userId]->setLoginStatus(true);
        users[userId]->ip = ip;
        users[userId]->port = port;
        clientUserMap[clientSock] = userId;

        // Update userIpPortMap
        userIpPortMap[userId] = make_pair(ip, port);

        response = "Login successful.";
    }
    pthread_mutex_unlock(&usersMutex);
}

void handleCreateGroup(const ArrayList<string>& tokens, int clientSock, string& response) {
    if (tokens.size() != 2) {
        response = "Usage: create_group <group_id>";
        return;
    }

    string groupId = tokens.get(1);

    pthread_mutex_lock(&groupsMutex);
    if (groups.find(groupId) != groups.end()) {
        response = "Error: Group already exists.";
    }
    else {
        pthread_mutex_lock(&usersMutex);
        if (clientUserMap.find(clientSock) == clientUserMap.end()) {
            response = "Error: Please login first.";
        }
        else {
            string userId = clientUserMap[clientSock];
            Group* newGroup = new Group(groupId, userId);
            groups[groupId] = newGroup;
            response = "Group created successfully.";
        }
        pthread_mutex_unlock(&usersMutex);
    }
    pthread_mutex_unlock(&groupsMutex);
}

void handleJoinGroup(const ArrayList<string>& tokens, int clientSock, string& response) {
    if (tokens.size() != 2) {
        response = "Usage: join_group <group_id>";
        return;
    }

    string groupId = tokens.get(1);

    pthread_mutex_lock(&groupsMutex);
    if (groups.find(groupId) == groups.end()) {
        response = "Error: Group does not exist.";
    }
    else {
        pthread_mutex_lock(&usersMutex);
        if (clientUserMap.find(clientSock) == clientUserMap.end()) {
            response = "Error: Please login first.";
        }
        else {
            string userId = clientUserMap[clientSock];
            Group* group = groups[groupId];
            bool isMember = false;
            for (int i = 0; i < group->members.size(); ++i) {
                if (group->members.get(i) == userId) {
                    isMember = true;
                    break;
                }
            }

            if (isMember) {
                response = "Error: Already a member of the group.";
            }
            else {
                // Add to pending requests
                group->pendingRequests.add(userId);
                response = "Join request sent to group owner.";
            }
        }
        pthread_mutex_unlock(&usersMutex);
    }
    pthread_mutex_unlock(&groupsMutex);
}

void handleLeaveGroup(const ArrayList<string>& tokens, int clientSock, string& response) {
    if (tokens.size() != 2) {
        response = "Usage: leave_group <group_id>";
        return;
    }

    string groupId = tokens.get(1);

    pthread_mutex_lock(&groupsMutex);
    if (groups.find(groupId) == groups.end()) {
        response = "Error: Group does not exist.";
    }
    else {
        pthread_mutex_lock(&usersMutex);
        if (clientUserMap.find(clientSock) == clientUserMap.end()) {
            response = "Error: Please login first.";
        }
        else {
            string userId = clientUserMap[clientSock];
            Group* group = groups[groupId];

            // Check if user is a member
            bool isMember = false;
            int memberIndex = -1;
            for (int i = 0; i < group->members.size(); ++i) {
                if (group->members.get(i) == userId) {
                    isMember = true;
                    memberIndex = i;
                    break;
                }
            }

            if (!isMember) {
                response = "Error: Not a member of the group.";
            }
            else {
                group->members.removeAt(memberIndex);
                response = "Left the group successfully.";
            }
        }
        pthread_mutex_unlock(&usersMutex);
    }
    pthread_mutex_unlock(&groupsMutex);
}

void handleListGroups(const ArrayList<string>& tokens, int clientSock, string& response) {
    pthread_mutex_lock(&groupsMutex);
    if (groups.empty()) {
        response = "No groups available.";
    }
    else {
        response = "Available groups:\n";
        for (auto it = groups.begin(); it != groups.end(); ++it) {
            response += it->first + "\n";
        }
    }
    pthread_mutex_unlock(&groupsMutex);
}

void handleListRequests(const ArrayList<string>& tokens, int clientSock, string& response) {
    if (tokens.size() != 2) {
        response = "Usage: list_requests <group_id>";
        return;
    }

    string groupId = tokens.get(1);

    pthread_mutex_lock(&groupsMutex);
    if (groups.find(groupId) == groups.end()) {
        response = "Error: Group does not exist.";
    }
    else {
        Group* group = groups[groupId];

        pthread_mutex_lock(&usersMutex);
        if (clientUserMap.find(clientSock) == clientUserMap.end()) {
            response = "Error: Please login first.";
        }
        else {
            string userId = clientUserMap[clientSock];
            if (group->ownerId != userId) {
                response = "Error: Only group owner can view pending requests.";
            }
            else {
                if (group->pendingRequests.isEmpty()) {
                    response = "No pending requests.";
                }
                else {
                    response = "Pending requests:\n";
                    for (int i = 0; i < group->pendingRequests.size(); ++i) {
                        response += group->pendingRequests.get(i) + "\n";
                    }
                }
            }
        }
        pthread_mutex_unlock(&usersMutex);
    }
    pthread_mutex_unlock(&groupsMutex);
}

void handleAcceptRequest(const ArrayList<string>& tokens, int clientSock, string& response) {
    if (tokens.size() != 3) {
        response = "Usage: accept_request <group_id> <user_id>";
        return;
    }

    string groupId = tokens.get(1);
    string userIdToAccept = tokens.get(2);

    pthread_mutex_lock(&groupsMutex);
    if (groups.find(groupId) == groups.end()) {
        response = "Error: Group does not exist.";
    }
    else {
        Group* group = groups[groupId];

        pthread_mutex_lock(&usersMutex);
        if (clientUserMap.find(clientSock) == clientUserMap.end()) {
            response = "Error: Please login first.";
        }
        else {
            string userId = clientUserMap[clientSock];
            if (group->ownerId != userId) {
                response = "Error: Only group owner can accept requests.";
            }
            else {
                // Check if userIdToAccept is in pendingRequests
                bool found = false;
                int requestIndex = -1;
                for (int i = 0; i < group->pendingRequests.size(); ++i) {
                    if (group->pendingRequests.get(i) == userIdToAccept) {
                        found = true;
                        requestIndex = i;
                        break;
                    }
                }

                if (!found) {
                    response = "Error: No such pending request.";
                }
                else {
                    
                    group->members.add(userIdToAccept);
                    group->pendingRequests.removeAt(requestIndex);
                    response = "User added to the group.";
                }
            }
        }
        pthread_mutex_unlock(&usersMutex);
    }
    pthread_mutex_unlock(&groupsMutex);
}

void handleListFiles(const ArrayList<string>& tokens, int clientSock, string& response) {
    if (tokens.size() != 2) {
        response = "Usage: list_files <group_id>";
        return;
    }

    string groupId = tokens.get(1);

    pthread_mutex_lock(&groupsMutex);
    if (groups.find(groupId) == groups.end()) {
        response = "Error: Group does not exist.";
    }
    else {
        pthread_mutex_lock(&usersMutex);
        if (clientUserMap.find(clientSock) == clientUserMap.end()) {
            response = "Error: Please login first.";
        }
        else {
            string userId = clientUserMap[clientSock];
            Group* group = groups[groupId];

            // Check if user is a member
            bool isMember = false;
            for (int i = 0; i < group->members.size(); ++i) {
                if (group->members.get(i) == userId) {
                    isMember = true;
                    break;
                }
            }

            if (!isMember) {
                response = "Error: Not a member of the group.";
            }
            else {
                // List files
                if (groupFiles.find(groupId) == groupFiles.end() || groupFiles[groupId].isEmpty()) {
                    response = "No files available in the group.";
                }
                else {
                    response = "Files in group " + groupId + ":\n";
                    ArrayList<File>& files = groupFiles[groupId];
                    for (int i = 0; i < files.size(); ++i) {
                        response += files.get(i).fileName + "\n";
                    }
                }
            }
        }
        pthread_mutex_unlock(&usersMutex);
    }
    pthread_mutex_unlock(&groupsMutex);
}

void handleUploadFile(const ArrayList<string>& tokens, int clientSock, string& response) {
    if (tokens.size() < 6) {
        response = "Usage: upload_file <file_name> <file_size> <file_sha1> <group_id> <chunk_sha1s...>";
        return;
    }

    string fileName = tokens.get(1);
    string fileSize = tokens.get(2);
    string fileSha1 = tokens.get(3);
    string groupId = tokens.get(4);

    // Collect chunk SHA1s
    ArrayList<string> chunkSha1s;
    for (int i = 5; i < tokens.size(); ++i) {
        chunkSha1s.add(tokens.get(i));
    }

    pthread_mutex_lock(&groupsMutex);
    if (groups.find(groupId) == groups.end()) {
        response = "Error: Group does not exist.";
        pthread_mutex_unlock(&groupsMutex);
        return;
    }

    pthread_mutex_lock(&usersMutex);
    if (clientUserMap.find(clientSock) == clientUserMap.end()) {
        response = "Error: Please login first.";
        pthread_mutex_unlock(&usersMutex);
        pthread_mutex_unlock(&groupsMutex);
        return;
    }

    string userId = clientUserMap[clientSock];
    Group* group = groups[groupId];

    // Check if user is a member
    bool isMember = false;
    for (int i = 0; i < group->members.size(); ++i) {
        if (group->members.get(i) == userId) {
            isMember = true;
            break;
        }
    }

    if (!isMember) {
        response = "Error: Not a member of the group.";
        pthread_mutex_unlock(&usersMutex);
        pthread_mutex_unlock(&groupsMutex);
        return;
    }

    // Check if file already exists in the group
    File* existingFile = nullptr;
    ArrayList<File>& files = groupFiles[groupId];
    for (int i = 0; i < files.size(); ++i) {
        if (files.get(i).fileName == fileName && files.get(i).fileSha1 == fileSha1) {
            existingFile = &files.get(i);
            break;
        }
    }

    if (existingFile) {
        // File exists; add user to userChunks if not already present
        if (existingFile->userChunks.find(userId) == existingFile->userChunks.end()) {
            existingFile->userChunks[userId] = ArrayList<int>();
            for (int i = 0; i < chunkSha1s.size(); ++i) {
                existingFile->userChunks[userId].add(i);
            }
            response = "File already exists. Added you as a sharer.";
        } else {
            response = "You are already sharing this file.";
        }
    } else {
        // File does not exist; add new file
        File newFile(fileName, fileSize, fileSha1, chunkSha1s);
        newFile.userChunks[userId] = ArrayList<int>();
        for (int i = 0; i < chunkSha1s.size(); ++i) {
            newFile.userChunks[userId].add(i);
        }
        groupFiles[groupId].add(newFile);
        response = "File uploaded successfully.";
    }

    pthread_mutex_unlock(&usersMutex);
    pthread_mutex_unlock(&groupsMutex);
}

void handleDownloadFile(const ArrayList<string>& tokens, int clientSock, string& response) {
    if (tokens.size() != 3) {
        response = "Usage: download_file <group_id> <file_name>";
        return;
    }

    string groupId = tokens.get(1);
    string fileName = tokens.get(2);

    pthread_mutex_lock(&groupsMutex);
    if (groups.find(groupId) == groups.end()) {
        response = "Error: Group does not exist.";
        pthread_mutex_unlock(&groupsMutex);
        return;
    }

    if (groupFiles.find(groupId) == groupFiles.end()) {
        response = "Error: No files available in the group.";
        pthread_mutex_unlock(&groupsMutex);
        return;
    }

    pthread_mutex_lock(&usersMutex);
    if (clientUserMap.find(clientSock) == clientUserMap.end()) {
        response = "Error: Please login first.";
        pthread_mutex_unlock(&usersMutex);
        pthread_mutex_unlock(&groupsMutex);
        return;
    }

    string userId = clientUserMap[clientSock];
    Group* group = groups[groupId];

    // Check if user is a member
    bool isMember = false;
    for (int i = 0; i < group->members.size(); ++i) {
        if (group->members.get(i) == userId) {
            isMember = true;
            break;
        }
    }

    if (!isMember) {
        response = "Error: Not a member of the group.";
        pthread_mutex_unlock(&usersMutex);
        pthread_mutex_unlock(&groupsMutex);
        return;
    }

    // Find the file
    ArrayList<File>& files = groupFiles[groupId];
    File* targetFile = nullptr;
    for (int i = 0; i < files.size(); ++i) {
        if (files.get(i).fileName == fileName) {
            targetFile = &files.get(i);
            break;
        }
    }

    if (targetFile == nullptr) {
        response = "Error: File not found in the group.";
        pthread_mutex_unlock(&usersMutex);
        pthread_mutex_unlock(&groupsMutex);
        return;
    }

    // Prepare download info
    stringstream ss;
    ss << "download_info ";
    ss << targetFile->fileSize << " ";
    int totalChunks = targetFile->chunkSha1s.size();
    ss << totalChunks << " ";
    ss << CHUNK_SIZE << " "; // Defined at the top
    ss << targetFile->fileSha1 << " ";

    for (int i = 0; i < totalChunks; ++i) {
        int chunkIndex = i;
        ArrayList<string> peersWithChunk;
        for (auto& userChunksEntry : targetFile->userChunks) {
            string peerUserId = userChunksEntry.first;
            ArrayList<int>& chunksOwned = userChunksEntry.second;
            for (int j = 0; j < chunksOwned.size(); ++j) {
                if (chunksOwned.get(j) == chunkIndex) {
                    peersWithChunk.add(peerUserId);
                    break;
                }
            }
        }

        ss << chunkIndex << " " << peersWithChunk.size() << " " << targetFile->chunkSha1s.get(i) << " ";
        for (int j = 0; j < peersWithChunk.size(); ++j) {
            string peerUserId = peersWithChunk.get(j);
            pair<string, int> ipPort = userIpPortMap[peerUserId];
            ss << peerUserId << " " << ipPort.first << " " << ipPort.second << " ";
        }
    }

    response = ss.str();

    pthread_mutex_unlock(&usersMutex);
    pthread_mutex_unlock(&groupsMutex);
}

void handleShutdown(const ArrayList<string>& tokens, int clientSock, string& response) {
    response = "Tracker is shutting down.";
    serverRunning = false;
}

void* clientHandler(void* socketDescPtr) {
    int clientSock = *(int*)socketDescPtr;
    free(socketDescPtr);

    // Assign a unique client ID
    pthread_mutex_lock(&clientsMutex);
    int clientID = clientIDCounter++;
    clientSockToID[clientSock] = clientID;
    connectedClients.add(clientSock);
    pthread_mutex_unlock(&clientsMutex);

    char buffer[BUFFER_SIZE];
    int readSize;

    while ((readSize = recv(clientSock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[readSize] = '\0';
        string command(buffer);
        cout << "\nReceived command from client " << clientID << ": " << command << endl;
        cout.flush();  // Ensure immediate output

        // Split the command into tokens
        ArrayList<string> tokens;
        char* commandCStr = new char[command.length() + 1];
        strcpy(commandCStr, command.c_str());
        char* tokenPtr = strtok(commandCStr, " \n");
        while (tokenPtr != NULL) {
            tokens.add(string(tokenPtr));
            tokenPtr = strtok(NULL, " \n");
        }
        delete[] commandCStr;

        string response;
        bool continueRunning = handleCommand(tokens, clientSock, response);

        response += "\n";  // Ensure response ends with a newline
        if (send(clientSock, response.c_str(), response.length(), 0) < 0) {
            alertPrompt("send failed", true);
            break;
        }

        if (!continueRunning && tokens.get(0) == "quit") {
            // Only disconnect the client, do not shut down the server
            break;
        }

        if (!continueRunning && tokens.get(0) == "shutdown") {
            break;
        }
    }

    if (readSize == 0) {
        pthread_mutex_lock(&clientsMutex);
        int clientID = clientSockToID[clientSock];
        pthread_mutex_unlock(&clientsMutex);

        printf("\nClient %d disconnected.\n", clientID);
        cout.flush();
    }
    else if (readSize == -1) {
        if (errno != EBADF) {  // Suppress EBADF error during shutdown
            alertPrompt("recv failed", true);
        }
    }
    pthread_mutex_lock(&usersMutex);
    auto it = clientUserMap.find(clientSock);
    if (it != clientUserMap.end()) {
        string userId = it->second;
        if (users.find(userId) != users.end()) {
            users.at(userId)->setLoginStatus(false);
            users.at(userId)->ip = "";
            users.at(userId)->port = 0;
        }
        clientUserMap.erase(it);
        userIpPortMap.erase(userId);
    }
    pthread_mutex_unlock(&usersMutex);
    pthread_mutex_lock(&clientsMutex);
    for (int i = 0; i < connectedClients.size(); ++i) {
        if (connectedClients.get(i) == clientSock) {
            connectedClients.removeAt(i);
            break;
        }
    }
    clientSockToID.erase(clientSock);
    pthread_mutex_unlock(&clientsMutex);

    close(clientSock);
    return NULL;
}

void* serverCommandHandler(void* arg) {
    while (serverRunning) {
        cout << "\nEnter server command: ";
        cout.flush(); 

        string command;
        getline(cin, command);

        if (command == "shutdown") {
            cout << "Initiating server shutdown..." << endl;

            // Lock the clients mutex to access connectedClients
            pthread_mutex_lock(&clientsMutex);
            
            for (int i = 0; i < connectedClients.size(); ++i) {
                int clientSock = connectedClients.get(i);
                string shutdownMsg = "shutdown\n";
                if (send(clientSock, shutdownMsg.c_str(), shutdownMsg.length(), 0) < 0) {
                    alertPrompt("send failed during shutdown", false);
                }
                close(clientSock);
            }
            
            connectedClients.clear();
            pthread_mutex_unlock(&clientsMutex);
            if (socketDesc != -1) {
                close(socketDesc);
                socketDesc = -1;
            }

            // Set serverRunning to false to stop the main loop
            serverRunning = false;
            break;
        }
        else if (!command.empty()) {
            cout << "Unknown command. Type 'shutdown' to stop the server." << endl;
        }
    }
    return NULL;
}

// --- Main Function ---

int main(int argc, char* argv[]) {
    signal(SIGINT, signalHandler);

    if (argc != 3) {
        alertPrompt("Please follow correct usage: " + string(argv[0]) + " <tracker_info.txt> <tracker_no>", false);
        exit(EXIT_FAILURE);
    }

    string trackerInfoFile = argv[1];
    int trackerNo = myAtoi(argv[2]);

    
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

    // Parse tracker info
    istringstream trackerInfoStream(trackerInfoBuffer);
    string trackerIp1, trackerIp2;
    string trackerPortStr1, trackerPortStr2;
    int trackerPort1, trackerPort2;
    try {
        trackerInfoStream >> trackerIp1 >> trackerPortStr1;
        trackerPort1 = myAtoi(trackerPortStr1);
        trackerInfoStream >> trackerIp2 >> trackerPortStr2;
        trackerPort2 = myAtoi(trackerPortStr2);
    }
    catch (...) {
        alertPrompt("Invalid format in tracker_info.txt", false);
        exit(EXIT_FAILURE);
    }

    string trackerIp;
    int trackerPort;
    if (trackerNo == 1) {
        trackerIp = trackerIp1;
        trackerPort = trackerPort1;
    }
    else if (trackerNo == 2) {
        trackerIp = trackerIp2;
        trackerPort = trackerPort2;
    }
    else {
        cerr << "Invalid tracker number" << endl;
        exit(EXIT_FAILURE);
    }

    int clientSock, c;
    struct sockaddr_in serverAddr, clientAddr;

    // Create socket
    socketDesc = socket(AF_INET, SOCK_STREAM, 0);
    if (socketDesc == -1) {
        alertPrompt("Could not create socket", true);
        exit(EXIT_FAILURE);
    }
    cout << "Socket created" << endl;

    // Prepare sockaddr_in structure
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr(trackerIp.c_str());
    serverAddr.sin_port = htons(trackerPort);

    // Bind
    if (::bind(socketDesc, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        alertPrompt("Bind failed", true);
        close(socketDesc);
        exit(EXIT_FAILURE);
    }
    cout << "Bind done" << endl;

    // Listen
    if (listen(socketDesc, 5) < 0) {
        alertPrompt("Listen failed", true);
        close(socketDesc);
        exit(EXIT_FAILURE);
    }
    cout << "Waiting for incoming connections on port " << trackerPort << "..." << endl;

    c = sizeof(struct sockaddr_in);

    // Create a thread to handle server commands (like shutdown)
    pthread_t commandThread;
    if (pthread_create(&commandThread, NULL, serverCommandHandler, NULL) != 0) {
        alertPrompt("Could not create server command handler thread", true);
        close(socketDesc);
        exit(EXIT_FAILURE);
    }

    // Accept incoming connections
    while (serverRunning && (clientSock = accept(socketDesc, (struct sockaddr*)&clientAddr, (socklen_t*)&c)) >= 0) {
        cout << "\nConnection accepted from " << inet_ntoa(clientAddr.sin_addr) << ":" << ntohs(clientAddr.sin_port) << endl;
        cout.flush();

        pthread_t clientThread;
        int* newSock = (int*)malloc(sizeof(int));
        if (newSock == NULL) {
            alertPrompt("Memory allocation failed for new socket", false);
            continue;
        }
        *newSock = clientSock;

        if (pthread_create(&clientThread, NULL, clientHandler, (void*)newSock) < 0) {
            alertPrompt("Could not create thread", true);
            free(newSock);
            continue;
        }

        pthread_detach(clientThread);
    }

    if (!serverRunning) {
        cout << "Server shutdown initiated." << endl;
    }
    else {
        alertPrompt("Accept failed", true);
    }

    // Close the main socket if it's still open
    if (socketDesc != -1) {
        close(socketDesc);
        socketDesc = -1;
    }

    cout << "Tracker closed gracefully." << endl;
    return 0;
}


// To Compile
// clang++ -std=c++11 tracker.cpp -o t -L/opt/homebrew/opt/openssl@3/lib -I/opt/homebrew/opt/openssl@3/include -lssl -lcrypto -pthread
// /Users/divsriv/Desktop