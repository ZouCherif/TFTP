#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/time.h>

#define SERVER_PORT 69
#define IP "127.0.0.1"
#define MAX_PACKET_SIZE 516
#define TIMEOUT_SECONDS 5

#define RRQ_OPCODE 1
#define WRQ_OPCODE 2
#define DATA_OPCODE 3
#define ACK_OPCODE 4
#define ERROR_OPCODE 5


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

void handle_request(int server_socket, struct sockaddr_in client_addr, char *filename, unsigned short opcode)
{
    switch (opcode)
    {
    case RRQ_OPCODE:
        handle_rrq(server_socket, client_addr, filename);
        break;
    case WRQ_OPCODE:
        handle_wrq(server_socket, client_addr, filename);
        break;
    default:
        printf("Opcode %d non supporté. Envoi d'un paquet d'erreur au client\n", opcode);
        send_error_packet(server_socket, client_addr, 1, "Opération non supportée");
        break;
    }
}

void handle_wrq(int server_socket, struct sockaddr_in client_addr, char *filename) {
    printf("Traitement de la demande d'écriture (WRQ) du client\n");

    int data_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (data_socket < 0) {
        perror("Erreur lors de la création du socket de données");
        send_error_packet(server_socket, client_addr, 1, "Erreur interne du serveur");
        return;
    }

    // Bind the data socket to any available port (0)
    struct sockaddr_in data_server_addr;
    memset(&data_server_addr, 0, sizeof(data_server_addr));
    data_server_addr.sin_family = AF_INET;
    data_server_addr.sin_addr.s_addr = INADDR_ANY;
    data_server_addr.sin_port = htons(0);
    if (bind(data_socket, (struct sockaddr *)&data_server_addr, sizeof(data_server_addr)) < 0) {
        perror("Erreur lors de la liaison du socket de données");
        send_error_packet(server_socket, client_addr, 1, "Erreur interne du serveur");
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
}


void handle_rrq(int server_socket, struct sockaddr_in client_addr, char *filename)
{
    printf("Traitement de la demande de lecture (RRQ) du client\n");

    // Create a separate socket for sending data packets
    int data_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (data_socket < 0)
    {
        perror("Erreur lors de la création du socket de données");
        send_error_packet(server_socket, client_addr, 1, "Erreur interne du serveur");
        return;
    }

    // Bind the data socket to any available port (0)
    struct sockaddr_in data_server_addr;
    memset(&data_server_addr, 0, sizeof(data_server_addr));
    data_server_addr.sin_family = AF_INET;
    data_server_addr.sin_addr.s_addr = INADDR_ANY;
    data_server_addr.sin_port = htons(0);
    if (bind(data_socket, (struct sockaddr *)&data_server_addr, sizeof(data_server_addr)) < 0)
    {
        perror("Erreur lors de la liaison du socket de données");
        send_error_packet(server_socket, client_addr, 1, "Erreur interne du serveur");
        close(data_socket);
        return;
    }

    // Get the port number assigned to the data socket
    struct sockaddr_in data_socket_addr;
    socklen_t data_socket_addr_len = sizeof(data_socket_addr);
    getsockname(data_socket, (struct sockaddr *)&data_socket_addr, &data_socket_addr_len);
    unsigned short data_port = ntohs(data_socket_addr.sin_port);

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

        // Send the data packet using the data socket
        if (sendto(data_socket, data_packet, 4 + bytes_read, 0, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0)
        {
            perror("Erreur lors de l'envoi du paquet de données");
            break;
        }
        printf("Sent data block %d (%ld bytes) to client on port %d\n", block_number, bytes_read, ntohs(client_addr.sin_port));

        char ack_packet[4];
        ssize_t bytes_received = recvfrom(server_socket, ack_packet, 4, 0, NULL, NULL);
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

        handle_request(server_socket, client_addr, request_packet + 2, opcode);
    }

    close(server_socket);
    return 0;
}
