#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/time.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>

#define SERVER_PORT 69
#define IP "127.0.0.1"
#define MAX_PACKET_SIZE 516
#define TIMEOUT_SECONDS 5
#define MAX_FILES 100

#define RRQ_OPCODE 1
#define WRQ_OPCODE 2
#define DATA_OPCODE 3
#define ACK_OPCODE 4
#define ERROR_OPCODE 5

void send_error_packet(int server_socket, struct sockaddr_in client_addr, int error_code, const char *error_message);
void *handle_request(void *arg);
void handle_wrq(int server_socket, struct sockaddr_in client_addr, char *filename);
void handle_rrq(int server_socket, struct sockaddr_in client_addr, char *filename);


pthread_mutex_t file_mutexes[MAX_PACKET_SIZE];
char *file_names[MAX_FILES];

struct ClientRequest {
    int server_socket;
    struct sockaddr_in client_addr;
    char filename[MAX_PACKET_SIZE];
    unsigned short opcode;
};

void init_file_mutexes() {
    for (int i = 0; i < MAX_PACKET_SIZE; ++i) {
        pthread_mutex_init(&file_mutexes[i], NULL);
    }
}

void destroy_file_mutexes() {
    for (int i = 0; i < MAX_PACKET_SIZE; ++i) {
        pthread_mutex_destroy(&file_mutexes[i]);
    }
}

void send_error_packet(int server_socket, struct sockaddr_in client_addr, int error_code, const char *error_message)
{
    char error_packet[MAX_PACKET_SIZE];
    memset(error_packet, 0, MAX_PACKET_SIZE);
    error_packet[0] = 0;
    error_packet[1] = ERROR_OPCODE;
    error_packet[2] = 0;
    error_packet[3] = error_code;
    strcpy(error_packet + 4, error_message);

    sendto(server_socket, error_packet, strlen(error_message) + 5, 0, (struct sockaddr *)&client_addr, sizeof(client_addr));
}

void *handle_request(void *arg) {
    struct ClientRequest *request = (struct ClientRequest *)arg;
    int file_index = -1;

    for (int i = 0; i < MAX_FILES; ++i) {
        if (file_names[i] != NULL && strcmp(request->filename, file_names[i]) == 0) {
            file_index = i;
            break;
        }
    }

    if (file_index == -1) {
        send_error_packet(request->server_socket, request->client_addr, 1, "Fichier introuvable");
        free(request);
        pthread_exit(NULL);
    }

    switch (request->opcode) {
        case RRQ_OPCODE:
            pthread_mutex_lock(&file_mutexes[file_index]);
            handle_rrq(request->server_socket, request->client_addr, request->filename);
            pthread_mutex_unlock(&file_mutexes[file_index]);
            break;
        case WRQ_OPCODE:
            pthread_mutex_lock(&file_mutexes[file_index]);
            handle_wrq(request->server_socket, request->client_addr, request->filename);
            pthread_mutex_unlock(&file_mutexes[file_index]);
            break;
        default:
            printf("Opcode %d non supporté. Envoi d'un paquet d'erreur au client\n", request->opcode);
            send_error_packet(request->server_socket, request->client_addr, 1, "Opération non supportée");
            break;
    }

    free(request);
    pthread_exit(NULL);
}

void handle_wrq(int server_socket, struct sockaddr_in client_addr, char *filename) {
    printf("Traitement de la demande d'écriture (WRQ) du client\n");

    int data_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (data_socket < 0) {
        perror("Erreur lors de la création du socket de données");
        send_error_packet(server_socket, client_addr, 1, "Erreur interne du serveur");
        return;
    }

    struct sockaddr_in data_server_addr;
    memset(&data_server_addr, 0, sizeof(data_server_addr));
    data_server_addr.sin_family = AF_INET;
    data_server_addr.sin_addr.s_addr = inet_addr(IP);
    data_server_addr.sin_port = htons(0);
    if (bind(data_socket, (struct sockaddr *)&data_server_addr, sizeof(data_server_addr)) < 0) {
        perror("Erreur lors de la liaison du socket de données");
        send_error_packet(server_socket, client_addr, 1, "Erreur interne du serveur");
        close(data_socket);
        return;
    }

    struct timeval timeout;
    timeout.tv_sec = TIMEOUT_SECONDS;
    timeout.tv_usec = 0;
    if (setsockopt(data_socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout)) < 0){
        perror("Erreur lors du réglage de l'option de délai d'attente");
        close(data_socket);
        return;
    }


    char ack_packet[4];
    ack_packet[0] = 0;
    ack_packet[1] = ACK_OPCODE;
    ack_packet[2] = 0;
    ack_packet[3] = 0;

    if (sendto(data_socket, ack_packet, sizeof(ack_packet), 0, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0) {
        perror("Erreur lors de l'envoi de l'ACK");
        close(data_socket);
        return;
    }
    

    FILE *file = fopen(filename, "wb");
    if (file == NULL) {
        send_error_packet(data_socket, client_addr, 1, "Impossible de créer le fichier");
        perror("Erreur lors de l'ouverture du fichier en écriture");
        close(data_socket);
        return;
    }

    unsigned short block_number = 1;
    while (1) {
        char data_packet[MAX_PACKET_SIZE];
        ssize_t bytes_received = recvfrom(data_socket, data_packet, MAX_PACKET_SIZE, 0, NULL, NULL);
        if (bytes_received < 0) {
            perror("Erreur lors de la réception du paquet de données");
            break;
        } else if (bytes_received == 0) {
            fprintf(stderr, "Connexion fermée par le client.\n");
            break;
        }

        if (data_packet[1] != DATA_OPCODE) {
            fprintf(stderr, "Paquet reçu n'est pas un paquet de données. Sortie...\n");
            break;
        }

        unsigned short received_block_number = (data_packet[2] << 8) | data_packet[3];
        if (received_block_number != block_number) {
            fprintf(stderr, "Numéro de bloc invalide. Sortie...\n");
            break;
        }

        size_t data_size = bytes_received - 4;
        fwrite(data_packet + 4, 1, data_size, file);

        ack_packet[2] = block_number >> 8;
        ack_packet[3] = block_number & 0xFF;
        if (sendto(data_socket, ack_packet, sizeof(ack_packet), 0, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0) {
            perror("Erreur lors de l'envoi de l'ACK");
            break;
        }

        if (data_size < 512)
            break;

        block_number++;
    }

    fclose(file);
    close(data_socket);

    printf("Done\n");
}


void handle_rrq(int server_socket, struct sockaddr_in client_addr, char *filename)
{
    printf("Traitement de la demande de lecture (RRQ) du client\n");

    int data_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (data_socket < 0){
        perror("Erreur lors de la création du socket de données");
        send_error_packet(server_socket, client_addr, 1, "Erreur interne du serveur");
        return;
    }

    struct sockaddr_in data_server_addr;
    memset(&data_server_addr, 0, sizeof(data_server_addr));
    data_server_addr.sin_family = AF_INET;
    data_server_addr.sin_addr.s_addr = inet_addr(IP);
    data_server_addr.sin_port = htons(0);
    if (bind(data_socket, (struct sockaddr *)&data_server_addr, sizeof(data_server_addr)) < 0)
    {
        perror("Erreur lors de la liaison du socket de données");
        send_error_packet(server_socket, client_addr, 1, "Erreur interne du serveur");
        close(data_socket);
        return;
    }

    struct timeval timeout;
    timeout.tv_sec = TIMEOUT_SECONDS;
    timeout.tv_usec = 0;
    if (setsockopt(data_socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout)) < 0){
        perror("Erreur lors du réglage de l'option de délai d'attente");
        close(data_socket);
        return;
    }

    FILE *file = fopen(filename, "rb");
    if (file == NULL)
    {
        send_error_packet(server_socket, client_addr, 1, "Fichier introuvable");
        perror("Erreur lors de l'ouverture du fichier en lecture");
        close(data_socket);
        return;
    }

    unsigned short block_number = 1;
    int attempts = 1;

    while (1)
    {
        char data_packet[MAX_PACKET_SIZE];
        ssize_t bytes_read = fread(data_packet + 4, 1, 512, file);

        data_packet[1] = DATA_OPCODE;
        data_packet[2] = block_number >> 8;
        data_packet[3] = block_number & 0xFF;

        if (sendto(data_socket, data_packet, 4 + bytes_read, 0, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0)
        {
            perror("Erreur lors de l'envoi du paquet de données");
            break;
        }
        printf("Sent data block %d (%ld bytes) to client on port %d\n", block_number, bytes_read, ntohs(client_addr.sin_port));

        char ack_packet[4];
        ssize_t bytes_received = recvfrom(data_socket, ack_packet, 4, 0, NULL, NULL);
        if (bytes_received < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                if (attempts >= 2)
                {
                    fprintf(stderr, "Nombre maximal de tentatives atteint. Sortie...\n");
                    break;
                }
                attempts++;
                fprintf(stderr, "Un délai d'attente s'est produit, nouvelle tentative...\n");
                continue;
            }
            else
            {
                perror("Erreur de réception du paquet ACK");
                break;
            }
        }
        else if (bytes_received == 0)
        {
            fprintf(stderr, "Connexion fermée par le client.\n");
            break;
        }

        if (ack_packet[1] != ACK_OPCODE || (ack_packet[2] != (block_number >> 8)) || (ack_packet[3] != (block_number & 0xFF)))
        {
            fprintf(stderr, "Paquet ACK invalide reçu. Sortie...\n");
            break;
        }

        block_number++;

        if (bytes_read < 512)
            break;
    }

    fclose(file);
    close(data_socket);
}

int main()
{
    int file_count = 0;
    DIR *dir;
    struct dirent *entry;
    struct stat stat_buf;

    dir = opendir(".");
    if (dir == NULL) {
        perror("Error opening server directory");
        exit(EXIT_FAILURE);
    }

    while ((entry = readdir(dir)) != NULL && file_count < MAX_FILES) {
        if (stat(entry->d_name, &stat_buf) == 0 && S_ISREG(stat_buf.st_mode))  {
            char *extension = strrchr(entry->d_name, '.');
            if (extension != NULL && strcmp(extension, ".txt") == 0) {
                file_names[file_count] = strdup(entry->d_name);
                if (file_names[file_count] == NULL) {
                    perror("Error copying filename");
                    exit(EXIT_FAILURE);
                }
                file_count++;
            }
        }
    }

    closedir(dir);

    init_file_mutexes();

    int server_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    server_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_socket < 0)
    {
        perror("Erreur lors de la création du socket serveur");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(IP);
    server_addr.sin_port = htons(SERVER_PORT);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Erreur de liaison du socket du serveur");
        exit(EXIT_FAILURE);
    }

    printf("Serveur en écoute sur le port %d...\n", SERVER_PORT);

    while (1)
    {
        char request_packet[MAX_PACKET_SIZE];
        ssize_t bytes_received = recvfrom(server_socket, request_packet, MAX_PACKET_SIZE, 0, (struct sockaddr *)&client_addr, &client_addr_len);
        if (bytes_received < 0)
        {
            perror("Erreur de réception du paquet de requête");
            continue;
        }
        unsigned short opcode;
        memcpy(&opcode, request_packet, sizeof(opcode));
        opcode = ntohs(opcode);

        struct ClientRequest *request = malloc(sizeof(struct ClientRequest));
        if (request == NULL) {
            perror("Erreur d'allocation de mémoire pour la requête client");
            continue;
        }

        request->server_socket = server_socket;
        request->client_addr = client_addr;
        strcpy(request->filename, request_packet + 2);
        request->opcode = opcode;

        pthread_t thread;
        if (pthread_create(&thread, NULL, handle_request, (void *)request) != 0) {
            perror("Erreur lors de la création du thread");
            free(request);
        }
    }

    close(server_socket);

    for (int i = 0; i < file_count; i++) {
        free(file_names[i]);
    }

    destroy_file_mutexes();
    return 0;
}
