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
    error_packet[0] = 0; // Adding a null byte for the opcode
    error_packet[1] = ERROR_OPCODE;
    error_packet[2] = 0; // Adding a null byte for the error code
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
        // handle_wrq(server_socket, client_addr, filename);
        break;
    default:
        printf("Unsupported opcode %d. Sending error packet to client\n", opcode);
        send_error_packet(server_socket, client_addr, 1, "Unsupported operation");
        break;
    }
}

void handle_rrq(int server_socket, struct sockaddr_in client_addr, char *filename)
{
    printf("Handling Read Request (RRQ) from client\n");

    FILE *file = fopen(filename, "rb");
    if (file == NULL)
    {
        send_error_packet(server_socket, client_addr, 1, "File not found");
        perror("Error opening file for reading");
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

        if (sendto(server_socket, data_packet, 4 + bytes_read, 0, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0)
        {
            perror("Error sending data packet");
            break;
        }

        char ack_packet[4];
        ssize_t bytes_received = recvfrom(server_socket, ack_packet, 4, 0, NULL, NULL);
        if (bytes_received < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                if (attempts >= 2)
                {
                    fprintf(stderr, "Max retry attempts reached. Exiting...\n");
                    break;
                }
                attempts++;
                fprintf(stderr, "Timeout occurred, retrying...\n");
                continue;
            }
            else
            {
                perror("Error receiving ACK packet");
                break;
            }
        }
        else if (bytes_received == 0)
        {
            fprintf(stderr, "Connection closed by client.\n");
            break;
        }

        if (ack_packet[1] != ACK_OPCODE || (ack_packet[2] != (block_number >> 8)) || (ack_packet[3] != (block_number & 0xFF)))
        {
            fprintf(stderr, "Invalid ACK packet received. Exiting...\n");
            break;
        }

        block_number++;

        if (bytes_read < 512)
            break;
    }

    fclose(file);
}

int main()
{
    int server_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    server_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_socket < 0)
    {
        perror("Error creating server socket");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(IP);
    server_addr.sin_port = htons(SERVER_PORT);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Error binding server socket");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", SERVER_PORT);

    while (1)
    {
        char request_packet[MAX_PACKET_SIZE];
        ssize_t bytes_received = recvfrom(server_socket, request_packet, MAX_PACKET_SIZE, 0, (struct sockaddr *)&client_addr, &client_addr_len);
        if (bytes_received < 0)
        {
            perror("Error receiving request packet");
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