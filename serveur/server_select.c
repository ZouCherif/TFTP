#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/time.h>
#include <dirent.h>
#include <sys/stat.h>

#define SERVER_PORT 69
#define IP "127.0.0.1"
#define MAX_PACKET_SIZE 516
#define TIMEOUT_SECONDS 5

#define RRQ_OPCODE 1
#define WRQ_OPCODE 2
#define DATA_OPCODE 3
#define ACK_OPCODE 4
#define ERROR_OPCODE 5
#define OACK_OPCODE 6

void send_error_packet(int server_socket, struct sockaddr_in client_addr, int error_code, const char *error_message);
void handle_wrq(int server_socket, struct sockaddr_in client_addr, char *filename, bool bigfile);
void handle_rrq(int server_socket, struct sockaddr_in client_addr, char *filename, bool bigfile);


void send_error_packet(int server_socket, struct sockaddr_in client_addr, int error_code, const char *error_message){
    char error_packet[MAX_PACKET_SIZE];
    memset(error_packet, 0, MAX_PACKET_SIZE);
    error_packet[0] = 0;
    error_packet[1] = ERROR_OPCODE;
    error_packet[2] = 0;
    error_packet[3] = error_code;
    strcpy(error_packet + 4, error_message);

    sendto(server_socket, error_packet, strlen(error_message) + 5, 0, (struct sockaddr *)&client_addr, sizeof(client_addr));
}

void handle_wrq(int server_socket, struct sockaddr_in client_addr, char *filename, bool bigfile) {
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


    unsigned char oack_packet[4];
    oack_packet[0] = 0;
    oack_packet[1] = OACK_OPCODE;
    oack_packet[2] = 0;
    oack_packet[3] = 0;

    if (sendto(data_socket, oack_packet, sizeof(oack_packet), 0, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0) {
        perror("Erreur lors de l'envoi de l'OACK");
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
    unsigned char ack_packet[4];
    ack_packet[0] = 0;
    ack_packet[1] = ACK_OPCODE;
    while (1) {
        unsigned char data_packet[MAX_PACKET_SIZE];
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

        if (block_number == 65535 && !bigfile) {
            fprintf(stderr, "Fichier trop volumineux. Sortie...\n");
            break;
        }

        block_number++;
        
        if (block_number == 0) {
            block_number = 1;
        }
    }

    fclose(file);
    close(data_socket);
}


void handle_rrq(int server_socket, struct sockaddr_in client_addr, char *filename, bool bigfile)
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

    unsigned char oack_packet[4];
    oack_packet[0] = 0;
    oack_packet[1] = OACK_OPCODE;
    oack_packet[2] = 0;
    oack_packet[3] = 0;

    if (sendto(data_socket, oack_packet, sizeof(oack_packet), 0, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0) {
        perror("Erreur lors de l'envoi de l'OACK");
        close(data_socket);
        return;
    }

    unsigned char ack_packet[4];
    ssize_t ack_recieved = recvfrom(data_socket, ack_packet, 4, 0, NULL, NULL);
    if(ack_recieved <= 0){
        perror("Erreur de réception du paquet ACK");
        close(data_socket);
        return;
    }
    if(ack_packet[1] != ACK_OPCODE){
        fprintf(stderr, "Paquet ACK invalide reçu. Sortie...\n");
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
        unsigned char data_packet[MAX_PACKET_SIZE];
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

        if (block_number == 65535 && !bigfile) {
            send_error_packet(server_socket, client_addr, 3, "Fichier trop volumineux");
            fprintf(stderr, "Fichier trop volumineux. Sortie...\n");
            break;
        }

        block_number++;

        if (block_number == 0) {
            block_number = 1;
        }

        if (bytes_read < 512)
            break;
    }

    fclose(file);
    close(data_socket);
}


int main(){
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

    fd_set read_fds;
    int max_fd = server_socket + 1;

    char request_packet[MAX_PACKET_SIZE];
    while (1)
    {
        FD_ZERO(&read_fds);
        FD_SET(server_socket, &read_fds);

        int activity = select(max_fd, &read_fds, NULL, NULL, NULL);
        if (activity < 0)
        {
            perror("Erreur dans select");
            continue;
        }

        if (FD_ISSET(server_socket, &read_fds))
        {
            memset(request_packet, 0, MAX_PACKET_SIZE);
            ssize_t bytes_received = recvfrom(server_socket, request_packet, MAX_PACKET_SIZE, 0, (struct sockaddr *)&client_addr, &client_addr_len);
            if (bytes_received < 0)
            {
                perror("Erreur de réception du paquet de requête");
                continue;
            }
            unsigned short opcode;
            memcpy(&opcode, request_packet, sizeof(opcode));
            opcode = ntohs(opcode);

            char filename[MAX_PACKET_SIZE];
            strcpy(filename, request_packet + 2);

            bool bigfile = false;
            char *option = request_packet + strlen(filename) + 9;
            while (*option != '\0') {
                if (strcmp(option, "bigfile") == 0) {
                    bigfile = true;
                    break;
                }
                option += strlen(option) + 1;
            }

            switch (opcode)
            {
            case RRQ_OPCODE:
                handle_rrq(server_socket, client_addr, filename, bigfile);
                break;
            case WRQ_OPCODE:
                handle_wrq(server_socket, client_addr, filename, bigfile);
                break;
            default:
                printf("Opcode %d non supporté. Envoi d'un paquet d'erreur au client\n", opcode);
                send_error_packet(server_socket, client_addr, 1, "Opération non supportée");
                break;
            }
        }
    }

    close(server_socket);

    return 0;
}
