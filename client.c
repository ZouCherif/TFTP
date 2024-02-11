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
#define TIMEOUT_SECONDS 5
#define MAX_RETRIES 3

#define RRQ_OPCODE 1
#define WRQ_OPCODE 2
#define DATA_OPCODE 3
#define ACK_OPCODE 4
#define ERROR_OPCODE 5

void handle_error_packet(const char *error_packet)
{
    int error_code = error_packet[3];
    const char *error_message = error_packet + 4;

    fprintf(stderr, "Error from server (Error code: %d): %s\n", error_code, error_message);
}

void handle_rrq(int client_socket, struct sockaddr_in server_addr, const char *filename)
{
    char rrq_packet[MAX_PACKET_SIZE];
    rrq_packet[0] = 0; // Opcode RRQ
    rrq_packet[1] = RRQ_OPCODE;
    strcpy(rrq_packet + 2, filename);
    rrq_packet[strlen(filename) + 2] = 0;

    if (sendto(client_socket, rrq_packet, strlen(filename) + 3, 0, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Error sending RRQ packet");
        return;
    }

    struct timeval timeout;
    timeout.tv_sec = TIMEOUT_SECONDS;
    timeout.tv_usec = 0;
    if (setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout)) < 0){
        perror("Error setting timeout option");
        return;
    }

    FILE *file = fopen(filename, "wb");
    if (file == NULL){
        char error_packet[MAX_PACKET_SIZE];
        memset(error_packet, 0, MAX_PACKET_SIZE);
        error_packet[0] = 0;
        error_packet[1] = ERROR_OPCODE;
        error_packet[2] = 0;
        strcpy(error_packet + 4, "Failed to create file");

        sendto(client_socket, error_packet, 4 + strlen("Failed to create file") + 1, 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
        return;
    }

    unsigned short block_number = 1;

    while (1)
    {
        char data_packet[MAX_PACKET_SIZE];
        ssize_t bytes_received = recvfrom(client_socket, data_packet, MAX_PACKET_SIZE, 0, NULL, NULL);
        if (data_packet[1] == 5){
            handle_error_packet(data_packet);
            break;
        }
        
        if (bytes_received < 0)
        {
            perror("Error receiving data packet");
            break;
        }
        else if (bytes_received == 0){
            fprintf(stderr, "Connection closed by server.\n");
            break;
        }

        fwrite(data_packet + 4, 1, bytes_received - 4, file);

        char ack_packet[4];
        ack_packet[0] = 0;
        ack_packet[1] = ACK_OPCODE;
        ack_packet[2] = block_number >> 8;
        ack_packet[3] = block_number & 0xFF;
        sendto(client_socket, ack_packet, 4, 0, (struct sockaddr *)&server_addr, sizeof(server_addr));

        if (bytes_received < 512){   
            break;
        }
        block_number++;
    }

    fclose(file);
}

int main(int argc, char *argv[])
{
    if (argc != 5)
    {
        fprintf(stderr, "Usage: %s put filename 127.0.0.1 69\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *operation = argv[1];
    const char *filename = argv[2];
    const char *server_ip = argv[3];
    const int server_port = atoi(argv[4]);

    int server_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_socket < 0)
    {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(server_ip);
    server_addr.sin_port = htons(server_port);

    if (strcmp(operation, "put") == 0){
        // handle_wrq(server_socket, server_addr, filename);
    }
    else if (strcmp(operation, "get") == 0){
        handle_rrq(server_socket, server_addr, filename);
    }
    else
    {
        fprintf(stderr, "Unsupported operation\n");
        exit(EXIT_FAILURE);
    }

    close(server_socket);
    return 0;
}