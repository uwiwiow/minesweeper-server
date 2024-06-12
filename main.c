#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>

#define MAX_CLIENTS 100

typedef struct Vector2 {
    float x;
    float y;
} Vector2;

typedef enum CellAction {
    NONE = -1, // no action
    OPENED, // open a tile
    CLEAR, // clear from flag or question
    FLAGGED, // flag tile
    QUESTIONED // question tile
} CellAction;

typedef struct Packet {
    Vector2 pos_cursor;
    CellAction action_tile;
} Packet;

typedef struct {
    int client_fd;
    Packet packet;
} Client;

Client clients[MAX_CLIENTS];
int num_clients = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

void remove_client(int client_fd) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < num_clients; i++) {
        if (clients[i].client_fd == client_fd) {
            clients[i] = clients[num_clients - 1];
            num_clients--;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void *handle_client(void *arg) {
    int client_fd = *((int *)arg);
    free(arg);

    Packet packet;

    while (1) {
        ssize_t nbytes_read = recv(client_fd, &packet, sizeof(Packet), 0);
        if (nbytes_read <= 0) {
            break;
        }

        if (packet.action_tile >= 0 ) {
            printf("Received Packet from client %d: Cursor(%f, %f), Action(%d)\n",
                   client_fd, packet.pos_cursor.x, packet.pos_cursor.y, packet.action_tile);
        }

        pthread_mutex_lock(&clients_mutex);
        for (int i = 0; i < num_clients; i++) {
            if (clients[i].client_fd == client_fd) {
                clients[i].packet = packet;
                break;
            }
        }

        Packet all_packets[MAX_CLIENTS];
        for (int i = 0; i < num_clients; i++) {
            all_packets[i] = clients[i].packet;
        }
        pthread_mutex_unlock(&clients_mutex);

        send(client_fd, all_packets, sizeof(Packet) * num_clients, 0);
    }

    close(client_fd);
    remove_client(client_fd);

    printf("Closed client connection %d\n", client_fd);
    pthread_exit(NULL);
}

int main() {
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr = {
            .sin_family = AF_INET,
            .sin_port = htons(12345),
            .sin_addr.s_addr = INADDR_ANY
    };

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

    printf("Server listening on port 12345\n");

    while (1) {
        int client_fd = accept(server_socket, NULL, NULL);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        pthread_mutex_lock(&clients_mutex);
        if (num_clients >= MAX_CLIENTS) {
            close(client_fd);
            pthread_mutex_unlock(&clients_mutex);
            continue;
        }

        clients[num_clients].client_fd = client_fd;
        num_clients++;
        pthread_mutex_unlock(&clients_mutex);

        int *new_client_fd = malloc(sizeof(int));
        *new_client_fd = client_fd;
        pthread_t thread;
        pthread_create(&thread, NULL, handle_client, new_client_fd);
        pthread_detach(thread);
    }

    close(server_socket);
    return 0;
}
