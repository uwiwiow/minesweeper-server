#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <netdb.h>
#include <signal.h>
#include <time.h>
#include <stdlib.h>
#include <stdarg.h>
#include "raylib.h"

#define MAX_CLIENTS 10
#define MAX_TEXTS 25
#define MAX_STRING_LENGTH 256

// Estructuras
typedef enum CellAction {
    NONE = -1,  // no action
    OPENED,     // open a tile
    CLEAR,      // clear from flag or question
    FLAGGED,    // flag tile
    QUESTIONED  // question tile
} CellAction;

typedef enum State {
    START,
    PLAYING,
    WIN,
    LOSE
} State;

typedef struct Packet {
    Vector2 pos_cursor;
    CellAction action_tile;
    unsigned int seed;
    State state;
} Packet;

typedef struct Client {
    int client_fd;
    Packet packet;
} Client;

char texts[MAX_TEXTS][MAX_STRING_LENGTH];
int text_count = 0;
unsigned int seed;
int restart = 0;
Client clients[MAX_CLIENTS];
int num_clients = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

void add_text(const char *format, ...) {
    if (text_count >= MAX_TEXTS) {
        for (int i = 1; i < MAX_TEXTS; i++) {
            strncpy(texts[i - 1], texts[i], MAX_STRING_LENGTH - 1);
            texts[i - 1][MAX_STRING_LENGTH - 1] = '\0';
        }
        text_count = MAX_TEXTS - 1;
    }

    va_list args;
    va_start(args, format);
    vsnprintf(texts[text_count], MAX_STRING_LENGTH, format, args);
    va_end(args);

    if (text_count < MAX_TEXTS) {
        text_count++;
    }
}

const char* cell_action_to_string(CellAction action) {
    switch (action) {
        case NONE:       return "NONE";
        case OPENED:     return "OPENED";
        case CLEAR:      return "CLEAR";
        case FLAGGED:    return "FLAGGED";
        case QUESTIONED: return "QUESTIONED";
        default:         return "UNKNOWN";
    }
}


void create_seed() {
    seed = (unsigned int)time(NULL);
}

void *check_restart(void *arg) {
    while (1) {
        if (restart) {
            create_seed();
            add_text("Seed restarted: %u", seed);
            restart = 0;
        }
        usleep(1000);
    }
    return NULL;
}

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

    add_text("Seed: %u\n", seed);

    Packet packet;

    while (1) {
        ssize_t nbytes_read = recv(client_fd, &packet, sizeof(Packet), 0);
        if (nbytes_read <= 0) {
            break;
        }

        if (packet.state == LOSE) {
            restart = 1;
        }

        if (packet.action_tile >= 0 ) {
            add_text("Client %d: %f, %f, %s\n",
                   client_fd, packet.pos_cursor.x, packet.pos_cursor.y, cell_action_to_string(packet.action_tile));
        }

        packet.seed = seed;

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

    add_text("Closed client connection %d\n", client_fd);
    pthread_exit(NULL);
}

void *server_thread(void *arg) {
    int server_socket = *((int *)arg);

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

        add_text("new client: %d", client_fd);

        create_seed();

        clients[num_clients].client_fd = client_fd;
        num_clients++;
        pthread_mutex_unlock(&clients_mutex);

        int *new_client_fd = malloc(sizeof(int));
        *new_client_fd = client_fd;
        pthread_t thread;
        pthread_create(&thread, NULL, handle_client, new_client_fd);
        pthread_detach(thread);
    }

    return NULL;
}

void handle_signal(int sig) {
    printf("Closing server...\n");
    CloseWindow();
    exit(0);
}

int main() {
    signal(SIGTERM, handle_signal);
    signal(SIGKILL, handle_signal);

    SetTraceLogLevel(LOG_WARNING);
    InitWindow(800, 600, "Minesweeper Server");


    int server_socket, s;
    struct addrinfo hints = {
            .ai_family = AF_INET,
            .ai_socktype = SOCK_STREAM,
            .ai_protocol = 0,
            .ai_flags = 0,
    };
    struct addrinfo *result, *rp;
    s = getaddrinfo(NULL, "12345", &hints, &result);
    if (s != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        exit(EXIT_FAILURE);
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        server_socket = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (server_socket == -1) continue;

        if (bind(server_socket, rp->ai_addr, rp->ai_addrlen) == 0) break;  // Success

        close(server_socket);
    }

    freeaddrinfo(result);  // No longer needed

    if (rp == NULL) {
        fprintf(stderr, "Could not bind\n");
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, 10) < 0) {
        perror("listen");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    add_text("Server listening on port 12345\n");

    pthread_t server_thread_id;
    pthread_create(&server_thread_id, NULL, server_thread, &server_socket);

    pthread_t thread_restart;
    pthread_create(&thread_restart, NULL, check_restart, NULL);

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(RAYWHITE);

        pthread_mutex_lock(&clients_mutex);
        for (int i = 0; i < text_count; i++) {
            DrawText(texts[i], 10, 20 + 20 * i, 20, BLACK);
        }
        pthread_mutex_unlock(&clients_mutex);

        EndDrawing();
    }

    close(server_socket);
    pthread_cancel(server_thread_id);
    pthread_join(server_thread_id, NULL);
    pthread_cancel(thread_restart);
    pthread_join(thread_restart, NULL);
    pthread_mutex_destroy(&clients_mutex);

    CloseWindow();
    return 0;
}
