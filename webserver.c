#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>

#define DEFAULT_PORT 80
#define BUFFER_SIZE 1024
// Default HTTP port and buffer size
int request_count = 0;
int total_received_bytes = 0;
int total_sent_bytes = 0;
pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;
// Variables to store server stats and mutex to protect them
void handle_client(int client_socket);
void send_404(int client_socket);
void send_static_file(int client_socket, const char *filepath);
void send_stats(int client_socket);
void send_calc_result(int client_socket, const char *query);
// Function prototypes
void *client_thread(void *arg) {
    int client_socket = *(int *)arg;
    free(arg);
    handle_client(client_socket);
    close(client_socket);
    return NULL;
}
// Function to handle a client connection
int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    int opt;

    while ((opt = getopt(argc, argv, "p:")) != -1) {
        switch (opt) {
            case 'p':
                port = atoi(optarg);
                break;
            default:
                fprintf(stderr, "Usage: %s [-p port]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
// Create a socket and bind it to the specified port
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, 10) < 0) {
        perror("listen");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", port);

    while (1) {
        int client_socket = accept(server_socket, NULL, NULL);
        if (client_socket < 0) {
            perror("accept");
            continue;
        }

        int *client_socket_ptr = malloc(sizeof(int));
        *client_socket_ptr = client_socket;
        pthread_t thread;
        pthread_create(&thread, NULL, client_thread, client_socket_ptr);
        pthread_detach(thread);
    }

    close(server_socket);
    return 0;
}
// Main server loop to accept incoming connections
void handle_client(int client_socket) {
    char buffer[BUFFER_SIZE];
    int received_bytes = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
    if (received_bytes < 0) {
        perror("recv");
        return;
    }

    buffer[received_bytes] = '\0';

    pthread_mutex_lock(&stats_mutex);
    request_count++;
    total_received_bytes += received_bytes;
    pthread_mutex_unlock(&stats_mutex);

    char method[16], path[256], protocol[16];
    sscanf(buffer, "%s %s %s", method, path, protocol);

    if (strcmp(method, "GET") != 0) {
        send_404(client_socket);
        return;
    }

    if (strncmp(path, "/static/", 8) == 0) {
        send_static_file(client_socket, path + 8);
    } else if (strcmp(path, "/stats") == 0) {
        send_stats(client_socket);
    } else if (strncmp(path, "/calc?", 6) == 0) {
        send_calc_result(client_socket, path + 6);
    } else {
        send_404(client_socket);
    }
}
// Function to handle a client connection
void send_404(int client_socket) {
    const char *response = "HTTP/1.1 404 Not Found\r\n"
                           "Content-Length: 13\r\n"
                           "Content-Type: text/plain\r\n"
                           "\r\n"
                           "404 Not Found";
    send(client_socket, response, strlen(response), 0);
    pthread_mutex_lock(&stats_mutex);
    total_sent_bytes += strlen(response);
    pthread_mutex_unlock(&stats_mutex);
}
// Function to send a 404 Not Found response
void send_static_file(int client_socket, const char *filepath) {
    char fullpath[512];
    snprintf(fullpath, sizeof(fullpath), "static/%s", filepath);

    int file = open(fullpath, O_RDONLY);
    if (file < 0) {
        send_404(client_socket);
        return;
    }

    struct stat file_stat;
    fstat(file, &file_stat);

    char header[BUFFER_SIZE];
    int header_length = snprintf(header, sizeof(header),
                                 "HTTP/1.1 200 OK\r\n"
                                 "Content-Length: %ld\r\n"
                                 "Content-Type: application/octet-stream\r\n"
                                 "\r\n",
                                 file_stat.st_size);
    send(client_socket, header, header_length, 0);

    char file_buffer[BUFFER_SIZE];
    int bytes_read;
    while ((bytes_read = read(file, file_buffer, sizeof(file_buffer))) > 0) {
        send(client_socket, file_buffer, bytes_read, 0);
    }

    close(file);

    pthread_mutex_lock(&stats_mutex);
    total_sent_bytes += header_length + file_stat.st_size;
    pthread_mutex_unlock(&stats_mutex);
}
// Function to send a static file
void send_stats(int client_socket) {
    char response[BUFFER_SIZE];
    int response_length = snprintf(response, sizeof(response),
                                   "HTTP/1.1 200 OK\r\n"
                                   "Content-Type: text/html\r\n"
                                   "\r\n"
                                   "<html><body>"
                                   "<h1>Server Stats</h1>"
                                   "<p>Requests: %d</p>"
                                   "<p>Received Bytes: %d</p>"
                                   "<p>Sent Bytes: %d</p>"
                                   "</body></html>",
                                   request_count, total_received_bytes, total_sent_bytes);
    send(client_socket, response, response_length, 0);

    pthread_mutex_lock(&stats_mutex);
    total_sent_bytes += response_length;
    pthread_mutex_unlock(&stats_mutex);
}
//  Function to send server stats
void send_calc_result(int client_socket, const char *query) {
    int a = 0, b = 0;
    sscanf(query, "a=%d&b=%d", &a, &b);
    int result = a + b;

    char response[BUFFER_SIZE];
    int response_length = snprintf(response, sizeof(response),
                                   "HTTP/1.1 200 OK\r\n"
                                   "Content-Type: text/plain\r\n"
                                   "\r\n"
                                   "Result: %d", result);
    send(client_socket, response, response_length, 0);

    pthread_mutex_lock(&stats_mutex);
    total_sent_bytes += response_length;
    pthread_mutex_unlock(&stats_mutex);
}