/**
 * @file server.c
 * @author Ivan Cankov 12219400 <e12219400@student.tuwien.ac.at>
 * @date 13.01.2024
 * @brief A simple HTTP server in C
 **/

#include "util.h"

const int BUFFER_SIZE = 32;
volatile int QUIT = 0;

void handler(int sig) {
    QUIT = 1;
}

/**
 * @brief Print a usage message to stderr and exit the process with EXIT_FAILURE.
 * @param process The name of the current process.
 */
static void usage(const char *process) {
    fprintf(stderr, "[%s] USAGE: %s [-p PORT] [-i INDEX] DOC_ROOT\n", process, process);
    exit(EXIT_FAILURE);
}

/**
 * @brief Validates the provided file.
 * @param file the file you would like to validate
 * @return 0 if successful -1 otherwise
 */
static int validateFile(char *file) {
    if (file == NULL) return 0;
    return (strspn(file, "/\\:*?\"<>|") != 0 || strlen(file) > 255) ? -1 : 0;
}

/**
 * @brief Validates the provided directory and if it is valid and does not yet exist it gets created.
 * @implnote This function mutates the original string if it is deemed a valid directory!
 * @param dir the directory you would like to validate
 * @return 0 if successful -1 otherwise
 */
static int validateDir(char **dir) {
    if (strpbrk(*dir, "\\:*?\"<>|.") != NULL) {
        return -1;
    }

    struct stat st = {0};
    return stat(*dir, &st);
}

/**
 * Concatenates the doc-root and the requested file and checks for existence.
 * @param path the requested file path
 * @param root the doc-root
 * @param fullPath a character array of length path + root + 2 or larger
 * @param maxLength the length of the full path
 * @return 0 if successful -1 otherwise
 */
static int getFullPath(const char *path, const char *root, char *fullPath, size_t maxLength) {
    size_t requiredLength = strlen(path) + strlen(root) + 2;
    if (requiredLength > maxLength) {
        return -1;
    }

    memset(fullPath, 0, sizeof(maxLength));
    strcpy(fullPath, root);

    if (path[0] != '/') {
        strcat(fullPath, "/");
    }

    strcat(fullPath, path);

    return 0;
}

/**
 * Validates an HTTP request.
 * @details can handle empty requests
 * @param request the request
 * @param path a NOT initialised array of characters that gets modified in the function
 * @param index the index file
 * @param root the doc-root
 * @return 200 if successful
 */
static int validateRequest(char *request, char **path, char *index, char *root) {
    char *type = strtok(request, " ");
    *path = strtok(NULL, " ");
    char *protocol = strtok(NULL, " ");

    if (type == NULL || *path == NULL || protocol == NULL) return 400;

    if (strncmp(protocol, "HTTP/1.1", 8) != 0 ) return 400;

    if (strncmp(*path, "TeaCold", 8) == 0 ) return 418;

    if (strncmp(type, "500", 3) == 0) return 500;

    if (strncmp(type, "GET", 3) != 0) return 501;

    if (strncmp(*path, "/", 1) == 0 && strlen(*path) == 1) *path = strdup(index);

    size_t maxLength = strlen(index) + strlen(*path) + 2;

    char fullPath[maxLength];
    
    if (getFullPath(*path, root, fullPath, maxLength) == -1) return 501;

    *path = strdup(fullPath);

    if (*path == NULL) return 501;

    if (access(*path, F_OK) == -1) return 404;

    if (access(*path, R_OK) == -1) return 403;

    return 200;
}

static const char* getContentType(char *path) {
    char *extension = strrchr(path, '.');
    const char *contentType;

    if (extension == NULL) {
        contentType = "Content-Type: text/plain\r\n";
    } else if (strcmp(extension, ".html") == 0 || strcmp(extension, ".htm") == 0) {
        contentType = "Content-Type: text/html\r\n";
    } else if (strcmp(extension, ".css") == 0) {
        contentType = "Content-Type: text/css\r\n";
    } else if (strcmp(extension, ".js") == 0) {
        contentType = "Content-Type: application/javascript\r\n";
    } else if (strcmp(extension, ".png") == 0) {
        contentType = "Content-Type: image/png\r\n";
    } else if (strcmp(extension, ".jpg") == 0 || strcmp(extension, ".jpeg") == 0) {
        contentType = "Content-Type: image/jpeg\r\n";
    } else if (strcmp(extension, ".gif") == 0) {
        contentType = "Content-Type: image/gif\r\n";
    } else if (strcmp(extension, ".json") == 0) {
        contentType = "Content-Type: application/json\r\n";
    } else {
        contentType = "Content-Type: text/plain\r\n";
    }

    return contentType;
}

/**
 * close two files
 * @param file1
 * @param file2
 */
static void closeFiles(FILE *file1, FILE *file2) {
    if (file1 != NULL) fclose(file1);

    if (file2 != NULL) fclose(file2);
}

/**
 * Writes a response to the client.
 * @param code the response code
 * @param response the response message
 * @param clientSocket the client socket
 * @param path the path of the file to be read from
 * @return 0 if everything went well -1 otherwise
 */
static void writeResponse(int code, const char *response, int clientSocket, char *path) {
    FILE *writeFile = fdopen(clientSocket, "r+");
    if(writeFile == NULL){
        return;
    }

    if(fprintf(writeFile, "HTTP/1.1 %d (%s)\r\n", code, response) == -1){
        fprintf(stderr, "Error writing to client.\n");
        fclose(writeFile);
        return;
    }

    if (code != 200) {
        fflush(writeFile);
        closeFiles(writeFile, NULL);
        return;
    }

    FILE *readFile = fopen(path, "r");
    if (readFile == NULL) {
        fprintf(stderr, "Error opening file.\n");
        fclose(writeFile);
        return;
    }

    time_t currentTime;
    if (time(&currentTime) == -1) {
        closeFiles(writeFile, readFile);
        return;
    }

    char timeString[100];
    if (strftime(timeString, sizeof(timeString), "%a, %d %b %y %T %Z", localtime(&currentTime)) == -1) {
        closeFiles(writeFile, readFile);
        return;
    }

    const char *contentType = getContentType(path);

    struct stat st;

    if (stat(path, &st) != 0) {
        fprintf(stderr, "Error retrieving file status.\n");
        closeFiles(writeFile, readFile);
        return;
    }

    if(fprintf(writeFile, "Date: %s\r\n%sContent-Length: %ld\r\nConnection: close\r\n\r\n", timeString, contentType, st.st_size) < 0){
        fprintf(stderr, "Error fprintf failed\n");
        closeFiles(writeFile, readFile);
        return;
    }

    size_t read = 0;
    char buffer[BUFFER_SIZE];

    while((read = fread(buffer, sizeof(char), BUFFER_SIZE, readFile)) != 0){
        fwrite(buffer, sizeof(char), read, writeFile);
    }

    fflush(writeFile);
    closeFiles(writeFile, readFile);
}

/**
 * receives the message header and prints it to stderr
 * @param clientSocket the client socket fd
 * @return the entire request allocated DYNAMICALLY
 */
static char* receiveHeader(int clientSocket) {
    char *request = NULL;
    char buffer[BUFFER_SIZE];
    size_t bytesRead = 0;
    size_t totalBytesRead = 8;

    while ((bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 2, 0)) > 0) {
        buffer[bytesRead] = '\0';
        totalBytesRead += bytesRead;

        char *temp = realloc(request, totalBytesRead);
        if (temp == NULL) {
            free(request);
            return strdup("ERROR 500 HTTP/1.1");
        }
        request = temp;
        request = strcat(request, buffer);

        if (strstr(buffer, "\r\n\r\n") != NULL) {
            break;
        }
    }

    fprintf(stderr, "%s", request);
    return request;
}

// SYNOPSIS
//     server [-p PORT] [-i INDEX] DOC_ROOT
// EXAMPLE
//     server -p 1280 -i index.html ˜/Documents/my_website/

/**
 * @brief Entrypoint of the programme. (Sets up and runs server)
 * @details the program uses the HTTP protocol and can send files that are encoded in plain text.
 * @param argc
 * @param argv
 * @return
 */
int main(int argc, char *argv[]) {
    char* portStr = NULL;
    char* index = NULL;
    char* root = NULL;

    int option;
    while ((option = getopt(argc, argv, "p:i:")) != -1) {
        switch (option) {
            case 'p':
                if (portStr != NULL) {
                    usage(argv[0]);
                }
                portStr = optarg;
                break;
            case 'i':
                if (index != NULL) {
                    usage(argv[0]);
                }
                index = optarg;
                break;
            case '?':
                usage(argv[0]);
                break;
            default:
                assert(0);
        }
    }

    if (argc - optind != 1) {
        usage(argv[0]);
    }
    root = argv[optind];

    if (validateDir(&root) != 0) {
        fprintf(stderr, "Invalid doc-root directory name.\n");
        exit(EXIT_FAILURE);
    }

    if (validateFile(index) != 0) {
        fprintf(stderr, "Invalid index file name.\n");
        exit(EXIT_FAILURE);
    }

    if (index == NULL) {
        index = "/index.html";
    }

    if (argc - optind != 1) {
        usage(argv[0]);
    }
    root = argv[optind];

    if (portStr == NULL) {
        portStr = "8080";
    }
    int port = parsePort(portStr);

    if (port == -1) {
        fprintf(stderr, "Invalid port number.\n");
        exit(EXIT_FAILURE);
    }

    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    // source: https://www.youtube.com/watch?v=r7mZ11j_3lo&t=333s
    const int backlog = 1;
    int serverSocket;

    struct addrinfo hints;
    struct addrinfo *results;
    struct addrinfo *record;

    memset(&hints, 0, sizeof(struct addrinfo));

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, portStr, &hints, &results) != 0) {
        fprintf(stderr, "Failed to translate server socket.\n");
        exit(EXIT_FAILURE);
    }

    for (record = results; record != NULL; record = record->ai_next) {
        serverSocket = socket(record->ai_family, record->ai_socktype, record->ai_protocol);
        if (serverSocket == -1) continue;
        int enable = 1;
        setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
        if (bind(serverSocket, record->ai_addr, record->ai_addrlen) == 0) break;
        close(serverSocket);
    }

    if (record == NULL) {
        fprintf(stderr, "Failed to create or connect client socket.\n");
        exit(EXIT_FAILURE);
    }

    freeaddrinfo(results);

    if (listen(serverSocket, backlog) == -1) {
        fprintf(stderr, "Failed to start server socket listen.\n");
        exit(EXIT_FAILURE);
    }

    while (QUIT == 0) {
        int clientSocket;
        if ((clientSocket = accept(serverSocket, record->ai_addr, &record->ai_addrlen)) < 0) {
            fprintf(stderr, "Failed to accept client socket.\n");
            continue;
        }

        char *request = receiveHeader(clientSocket);

        char **path = malloc(sizeof(request));
        switch (validateRequest(request, path, index, root)) {
            case 200:
                writeResponse(200, "OK", clientSocket, *path);
                break;
            case 400:
                writeResponse(400, "Bad Request", clientSocket, *path);
                break;
            case 403:
                writeResponse(403, "Forbidden", clientSocket, *path);
                break;
            case 404:
                writeResponse(404, "Not Found", clientSocket, *path);
                break;
            case 418:
                writeResponse(418, "Tea Cold", clientSocket, *path);
                break;
            case 500:
                writeResponse(500, "Internal Server Error", clientSocket, *path);
                break;
            case 501:
                writeResponse(501, "Not Implemented", clientSocket, *path);
                break;
            default:
                assert(0);
        }

        free(request);
        free(path);
    }

    exit(EXIT_SUCCESS);
}
