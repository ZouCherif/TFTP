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
#define MAX_PACKET_SIZE 516
#define TIMEOUT_SECONDS 10

#define RRQ_OPCODE 1
#define WRQ_OPCODE 2
#define DATA_OPCODE 3
#define ACK_OPCODE 4
#define ERROR_OPCODE 5
#define OACK_OPCODE 6

void handle_error_packet(const char *error_packet)
{
    int error_code = error_packet[3];
    const char *error_message = error_packet + 4;

    fprintf(stderr, "Erreur du serveur (Code d'erreur: %d): %s\n", error_code, error_message);
}

void handle_wrq(int client_socket, struct sockaddr_in server_addr, const char *filename, const char *bigfile){
    char wrq_packet[MAX_PACKET_SIZE];
    wrq_packet[0] = 0;
    wrq_packet[1] = WRQ_OPCODE;
    strcpy(wrq_packet + 2, filename);
    wrq_packet[strlen(filename) + 2] = 0;
    strcpy(wrq_packet + strlen(filename) + 3, "octet");
    wrq_packet[strlen(filename) + 8] = 0;

    if(bigfile != NULL){
        strcpy(wrq_packet + strlen(filename) + 9, bigfile);
        wrq_packet[strlen(filename) + strlen(bigfile) + 9] = 0;
    }

    size_t packet_length = strlen(filename) + 9;

    if (bigfile != NULL) {
        packet_length += strlen(bigfile) + 1;
    }

    if (sendto(client_socket, wrq_packet, packet_length, 0, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Erreur lors de l'envoi du paquet WRQ");
        return;
    }

    struct timeval timeout;
    timeout.tv_sec = TIMEOUT_SECONDS;
    timeout.tv_usec = 0;
    if (setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout)) < 0){
        perror("Erreur lors du réglage de l'option de délai d'attente");
        return;
    }

    struct sockaddr_in server_data_addr;
    memset(&server_data_addr, 0, sizeof(server_data_addr));
    socklen_t server_data_addr_len = sizeof(server_data_addr);
    unsigned char oack_packet[4];
    ssize_t ack_recv = recvfrom(client_socket, oack_packet, sizeof(oack_packet), 0, (struct sockaddr *)&server_data_addr, &server_data_addr_len);
    if (ack_recv < 0) {
        perror("Erreur lors de la réception de l'OACK");
        return;
    } else if (ack_recv == 0) {
        fprintf(stderr, "Connexion fermée par le serveur.\n");
        return;
    }

    if (oack_packet[1] != OACK_OPCODE) {
        fprintf(stderr, "Paquet reçu n'est pas un OACK.\n");
        return;
    }

    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        perror("Erreur lors de l'ouverture du fichier en écriture");
        close(client_socket);
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

        if (sendto(client_socket, data_packet, 4 + bytes_read, 0, (struct sockaddr *)&server_data_addr, server_data_addr_len) < 0)
        {
            perror("Erreur lors de l'envoi du paquet de données");
            break;
        }

        unsigned char ack_packet[4];
        ssize_t bytes_received = recvfrom(client_socket, ack_packet, 4, 0, NULL, NULL);
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
    close(client_socket);

}


void handle_rrq(int client_socket, struct sockaddr_in server_addr, const char *filename, const char *bigfile)
{
    char rrq_packet[MAX_PACKET_SIZE];
    rrq_packet[0] = 0;
    rrq_packet[1] = RRQ_OPCODE;
    strcpy(rrq_packet + 2, filename);
    rrq_packet[strlen(filename) + 2] = 0;
    strcpy(rrq_packet + strlen(filename) + 3, "octet");
    rrq_packet[strlen(filename) + 8] = 0;

    if(bigfile != NULL){
        strcpy(rrq_packet + strlen(filename) + 9, bigfile);
        rrq_packet[strlen(filename) + strlen(bigfile) + 9] = 0;
    }

    size_t packet_length = strlen(filename) + 9;

    if (bigfile != NULL) {
        packet_length += strlen(bigfile) + 1;
    }

    if (sendto(client_socket, rrq_packet, packet_length, 0, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Erreur lors de l'envoi du paquet RRQ");
        return;
    }

    struct timeval timeout;
    timeout.tv_sec = TIMEOUT_SECONDS;
    timeout.tv_usec = 0;
    if (setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout)) < 0){
        perror("Erreur lors du réglage de l'option de délai d'attente");
        return;
    }

    struct sockaddr_in server_data_addr;
    memset(&server_data_addr, 0, sizeof(server_data_addr));
    socklen_t server_data_addr_len = sizeof(server_data_addr);

    //attendre l'OACK
    socklen_t addr_len = sizeof(server_addr);
    unsigned char oack_packet[4];
    ssize_t oack_recv = recvfrom(client_socket, oack_packet, sizeof(oack_packet), 0, (struct sockaddr *)&server_data_addr, &server_data_addr_len);
    if (oack_recv < 0) {
        perror("Erreur lors de la réception de l'OACK");
        return;
    } else if (oack_recv == 0) {
        fprintf(stderr, "Connexion fermée par le serveur.\n");
        return;
    }

    if (oack_packet[1] != OACK_OPCODE) {
        fprintf(stderr, "Paquet reçu n'est pas un OACK.\n");
        return;
    }

    //envoyer ACK
    unsigned char ack_packet[4];
    ack_packet[0] = 0;
    ack_packet[1] = ACK_OPCODE;
    ack_packet[2] = 0;
    ack_packet[3] = 0;
    if (sendto(client_socket, ack_packet, 4, 0, (struct sockaddr *)&server_data_addr, server_data_addr_len) < 0){
        perror("Erreur lors de l'envoi du ACK");
        return;
    }

    FILE *file = fopen(filename, "wb");
    if (file == NULL){
        char error_packet[MAX_PACKET_SIZE];
        memset(error_packet, 0, MAX_PACKET_SIZE);
        error_packet[0] = 0;
        error_packet[1] = ERROR_OPCODE;
        error_packet[2] = 0;
        strcpy(error_packet + 4, "Impossible de créer le fichier");

        sendto(client_socket, error_packet, 4 + strlen("Impossible de créer le fichier") + 1, 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
        return;
    }

    unsigned short block_number = 1;

    while (1)
    {
        unsigned char data_packet[MAX_PACKET_SIZE];
        ssize_t bytes_received = recvfrom(client_socket, data_packet, MAX_PACKET_SIZE, 0, (struct sockaddr *)&server_data_addr, &server_data_addr_len);

        if (data_packet[1] == 5){
            handle_error_packet(data_packet);
            break;
        }
        
        if (bytes_received < 0)
        {
            perror("Erreur lors de la réception du paquet de données");
            break;
        }
        else if (bytes_received == 0){
            fprintf(stderr, "Connexion fermée par le serveur.\n");
            break;
        }

        fwrite(data_packet + 4, 1, bytes_received - 4, file);

        unsigned char ack_packet[4];
        ack_packet[0] = 0;
        ack_packet[1] = ACK_OPCODE;
        ack_packet[2] = block_number >> 8;
        ack_packet[3] = block_number & 0xFF;
        sendto(client_socket, ack_packet, 4, 0, (struct sockaddr *)&server_data_addr, server_data_addr_len);

        if (bytes_received < 516){   
            break;
        }
        block_number++;
        if(block_number == 0){
            block_number = 1;
        }
    }

    fclose(file);
}

int main(int argc, char *argv[])
{
    if (argc < 5)
    {
        fprintf(stderr, "Utilisation: %s <get/put> <nom_de_fichier> 127.0.0.1 69 <bigfile (optionnel)>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *operation = argv[1];
    const char *filename = argv[2];
    const char *server_ip = argv[3];
    const int server_port = atoi(argv[4]);
    const char *bigfile = NULL;
    if (argc == 6){
        bigfile = argv[5];
        if(strcmp(bigfile, "bigfile") != 0){
            printf("Erreur: option non trouvé '%s'\n", argv[5]);
            exit(EXIT_FAILURE);
        }
    }

    int client_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (client_socket < 0)
    {
        perror("Erreur lors de la création de la socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(server_ip);
    server_addr.sin_port = htons(server_port);

    if (strcmp(operation, "put") == 0){
        handle_wrq(client_socket, server_addr, filename, bigfile);
    }
    else if (strcmp(operation, "get") == 0){
        handle_rrq(client_socket, server_addr, filename, bigfile);
    }
    else
    {
        fprintf(stderr, "Opération non supportée\n");
        exit(EXIT_FAILURE);
    }

    close(client_socket);
    return 0;
}
