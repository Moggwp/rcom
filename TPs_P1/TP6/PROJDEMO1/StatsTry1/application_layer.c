// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <math.h>
#include <sys/time.h>

extern int total_retransmissions;

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    LinkLayer linkLayer;
    TransferStats stats = {0};
    struct timeval start_time, end_time;

    strcpy(linkLayer.serialPort, serialPort);
    linkLayer.role = strcmp(role, "tx") ? LlRx : LlTx;
    linkLayer.baudRate = baudRate;
    linkLayer.nRetransmissions = nTries;
    linkLayer.timeout = timeout;

    printf("Starting link-layer protocol application\n");
    printf("  - Serial port: %s\n", linkLayer.serialPort);
    printf("  - Role: %s\n", role);
    printf("  - Baudrate: %d\n", linkLayer.baudRate);
    printf("  - Number of tries: %d\n", linkLayer.nRetransmissions);
    printf("  - Timeout: %d\n", linkLayer.timeout);
    printf("  - Filename: %s\n", filename);

    int fd = llopen(linkLayer);
    if (fd < 0) {
        perror("Connection error\n");
        exit(-1);
    }

    gettimeofday(&start_time, NULL);

    switch (linkLayer.role) {
        case LlTx: {
            FILE* file = fopen(filename, "rb");
            if (file == NULL) {
                perror("File not found\n");
                exit(-1);
            }

            int prev = ftell(file);
            fseek(file, 0L, SEEK_END);
            long int fileSize = ftell(file) - prev;
            fseek(file, prev, SEEK_SET);
            stats.file_size = fileSize;

            unsigned int cpSize;
            unsigned char *controlPacketStart = getControlPacket(2, filename, fileSize, &cpSize);
            if (llwrite(controlPacketStart, cpSize) == -1) { 
                printf("Exit: error in start packet\n");
                free(controlPacketStart);
                fclose(file);
                exit(-1);
            }
            free(controlPacketStart);

            unsigned char sequence = 0;
            unsigned char* content = getData(file, fileSize);
            unsigned char* content_ptr = content;
            long int bytesLeft = fileSize;

            while (bytesLeft > 0) {
                int dataSize = bytesLeft > (long int) MAX_PAYLOAD_SIZE ? MAX_PAYLOAD_SIZE : bytesLeft;
                unsigned char* data = (unsigned char*) malloc(dataSize);
                memcpy(data, content_ptr, dataSize);
                int packetSize;
                unsigned char* packet = getDataPacket(sequence, data, dataSize, &packetSize);
                
                if (llwrite(packet, packetSize) == -1) {
                    printf("Exit: error in data packets\n");
                    free(data);
                    free(packet);
                    free(content);
                    fclose(file);
                    exit(-1);
                }
                
                stats.packets_sent++;
                bytesLeft -= dataSize;
                content_ptr += dataSize;
                sequence = (sequence + 1) % 255;
                free(data);
                free(packet);
            }
            free(content);

            unsigned char *controlPacketEnd = getControlPacket(3, filename, fileSize, &cpSize);
            if (llwrite(controlPacketEnd, cpSize) == -1) { 
                printf("Exit: error in end packet\n");
                free(controlPacketEnd);
                fclose(file);
                exit(-1);
            }
            free(controlPacketEnd);

            fclose(file);
            break;
        }

        case LlRx: {
            unsigned char *packet = (unsigned char *) malloc(MAX_PAYLOAD_SIZE);
            int packetSize = -1;
            while ((packetSize = llread(packet)) < 0);
            unsigned long int rxFileSize = 0;
            unsigned char* name = parseControlPacket(packet, packetSize, &rxFileSize);
            stats.file_size = rxFileSize;

            char* forcedName = "penguin-received.gif";
            printf("Creating new file: %s\n", forcedName);

            FILE* newFile = fopen(forcedName, "wb+");
            if (!newFile) {
                perror("Erro ao criar arquivo\n");
                free(name);
                free(packet);
                exit(-1);
            }

            while (1) {    
                while ((packetSize = llread(packet)) < 0);
                if (packetSize == 0) break;
                else if (packet[0] != 3) {
                    unsigned char *buffer = (unsigned char*) malloc(packetSize - 4);
                    parseDataPacket(packet, packetSize, buffer);
                    fwrite(buffer, sizeof(unsigned char), packetSize - 4, newFile);
                    free(buffer);
                    stats.packets_received++;
                } else {
                    break;
                }
            }

            fclose(newFile);
            free(name);
            free(packet);
            llclose(fd);
            break;
        }

        default:
            exit(-1);
            break;
    }

    gettimeofday(&end_time, NULL);
    stats.transfer_time = (end_time.tv_sec - start_time.tv_sec) + 
                          (end_time.tv_usec - start_time.tv_usec) / 1000000.0;
    stats.throughput = stats.file_size / stats.transfer_time;

    #ifdef LINK_LAYER_RETRANSMISSIONS
    stats.retransmissions = total_retransmissions;
    #endif

    printf("\n--- Transfer Statistics ---\n");
    printf("File Size: %ld bytes\n", stats.file_size);
    if (linkLayer.role == LlTx) {
        printf("Packets Sent: %d\n", stats.packets_sent);
    } else {
        printf("Packets Received: %d\n", stats.packets_received);
    }
    printf("Transfer Time: %.3f seconds\n", stats.transfer_time);
    printf("Throughput: %.2f bytes/second\n", stats.throughput);
    #ifdef LINK_LAYER_RETRANSMISSIONS
    printf("Retransmissions: %d\n", stats.retransmissions);
    if (linkLayer.role == LlTx && stats.packets_sent > 0) {
        printf("Error Rate: %.2f%%\n", (stats.retransmissions / (float)stats.packets_sent) * 100);
    }
    #endif

    if (linkLayer.role == LlTx) llclose(fd);
}

unsigned char* parseControlPacket(unsigned char* packet, int size, unsigned long int *fileSize) {
    unsigned char fileSizeNBytes = packet[2];
    unsigned char fileSizeAux[fileSizeNBytes];
    memcpy(fileSizeAux, packet + 3, fileSizeNBytes);
    *fileSize = 0;
    for (unsigned int i = 0; i < fileSizeNBytes; i++)
        *fileSize |= (fileSizeAux[fileSizeNBytes - i - 1] << (8 * i));

    unsigned char fileNameNBytes = packet[3 + fileSizeNBytes + 1];
    unsigned char *name = (unsigned char*) malloc(fileNameNBytes + 1);
    memcpy(name, packet + 3 + fileSizeNBytes + 2, fileNameNBytes);
    name[fileNameNBytes] = '\0';
    return name;
}

unsigned char * getControlPacket(const unsigned int c, const char* filename, long int length, unsigned int* size) {
    const int L1 = (int) ceil(log2f((float)length) / 8.0);
    const int L2 = strlen(filename);
    *size = 1 + 2 + L1 + 2 + L2;
    unsigned char *packet = (unsigned char*) malloc(*size);
    
    unsigned int pos = 0;
    packet[pos++] = c;
    packet[pos++] = 0;
    packet[pos++] = L1;

    for (unsigned char i = 0; i < L1; i++) {
        packet[2 + L1 - i] = length & 0xFF;
        length >>= 8;
    }
    pos += L1;
    packet[pos++] = 1;
    packet[pos++] = L2;
    memcpy(packet + pos, filename, L2);
    return packet;
}

unsigned char * getDataPacket(unsigned char sequence, unsigned char *data, int dataSize, int *packetSize) {
    *packetSize = 1 + 1 + 2 + dataSize;
    unsigned char* packet = (unsigned char*) malloc(*packetSize);

    packet[0] = 1;   
    packet[1] = sequence;
    packet[2] = dataSize >> 8 & 0xFF;
    packet[3] = dataSize & 0xFF;
    memcpy(packet + 4, data, dataSize);

    return packet;
}

unsigned char * getData(FILE* fd, long int fileLength) {
    unsigned char* content = (unsigned char*) malloc(sizeof(unsigned char) * fileLength);
    if (!content) {
        perror("Erro ao alocar memória para os dados");
        return NULL;
    }
    size_t bytesRead = fread(content, sizeof(unsigned char), fileLength, fd);
    if (bytesRead < fileLength) {
        printf("Aviso: nem todos os bytes foram lidos do arquivo!\n");
    }
    return content;
}

void parseDataPacket(const unsigned char* packet, const unsigned int packetSize, unsigned char* buffer) {
    memcpy(buffer, packet + 4, packetSize - 4);
}