// Application layer implementation for file transfer over serial link

#include "application_layer.h"
#include "link_layer.h"
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <math.h>
#include <sys/time.h>

extern int total_retransmissions;

// Main function to handle file transfer
void applicationLayer(const char *serialPort, const char *device_role, int BaudRateConnect,
                       int max_attempts, int timeout, const char *file_path) {
    LinkLayer connection;
    TransferStats stats = {0};
    struct timeval transfer_starttime, transfer_endtime;

    // Initialize connection parameters
    strncpy(connection.serialPort, serialPort, sizeof(connection.serialPort) - 1);
    connection.serialPort[sizeof(connection.serialPort) - 1] = '\0';
    connection.role = (strcmp(device_role, "tx") == 0) ? LlTx : LlRx;
    connection.baudRate = BaudRateConnect;
    connection.nRetransmissions = max_attempts;
    connection.timeout = timeout;

    // Establish link layer connection
    int link_fd = llopen(connection);
    if (link_fd < 0) {
        fprintf(stderr, "Failed to establish connection\n");
        exit(-1);
    }

    gettimeofday(&transfer_starttime, NULL);

    if (connection.role == LlTx) {
        // Transmitter: Send file
        FILE *source_file = fopen(file_path, "rb");
        if (!source_file) {
            fprintf(stderr, "Unable to open source file: %s\n", file_path);
            exit(-1);
        }

        // Calculate file size
        fseek(source_file, 0, SEEK_END); //file pointer at the end
        long file_length = ftell(source_file); //returns the current position of the file pointer in bytes from the beginning of the file.
        fseek(source_file, 0, SEEK_SET);
        stats.file_size = file_length;

        // Send start control packet
        unsigned int start_packet_len;
        unsigned char *start_packet = createControlPacket(2, file_path, file_length, &start_packet_len);
        if (llwrite(start_packet, start_packet_len) == -1) { //sends it with llwrite
            fprintf(stderr, "Failed to send start packet\n");
            free(start_packet);
            fclose(source_file);
            exit(-1);
        }
        free(start_packet);

        // Send data packets: Read and send file data
        unsigned char packet_number = 0;
        unsigned char *file_data = readFileContent(source_file, file_length); //reads the entire file content into memory
        unsigned char *data_pointer = file_data;
        long remaining_bytes = file_length;

        //Tx: progress bar showing packets send
        float last_progress = -1.0; //track last displayed progress
        //sending packets loop:
        while (remaining_bytes > 0) {
            int chunk_size = (remaining_bytes > MAX_PAYLOAD_SIZE) ? MAX_PAYLOAD_SIZE : remaining_bytes; //chunk size up to max payload size
            unsigned char *chunk_data = malloc(chunk_size);
            memcpy(chunk_data, data_pointer, chunk_size);

            int data_packet_len;
            unsigned char *data_packet = buildDataPacket(packet_number, chunk_data, chunk_size, &data_packet_len);
            //printf("Sending: packet length = %d, chunk size = %d\n", data_packet_len, chunk_size);

            if (llwrite(data_packet, data_packet_len) == -1) {
                fprintf(stderr, "Failed to send data packet\n");
                free(chunk_data);
                free(data_packet);
                free(file_data);
                fclose(source_file);
                exit(-1);
            }

            stats.packets_transmitted++; //updates
            remaining_bytes -= chunk_size;
            data_pointer += chunk_size;
            packet_number = (packet_number + 1) % 255;

            //calculate bar progress
            float progress = ((float)(file_length - remaining_bytes) / file_length) * 100;
            if ((int)progress != (int)last_progress) {
                display_progress_bar(progress, 50);
                last_progress = progress; //increases
            }
            free(chunk_data);
            free(data_packet);
        }
        //free(file_data);

        // Send end control packet

        //100% progress bar
        display_progress_bar(100, 50);
        printf("\n");
        free(file_data);

        //end packet
        unsigned int end_packet_len;
        unsigned char *end_packet = createControlPacket(3, file_path, file_length, &end_packet_len);
        if (llwrite(end_packet, end_packet_len) == -1) {
            fprintf(stderr, "Failed to send end packet\n");
            free(end_packet);
            fclose(source_file);
            exit(-1);
        }
        free(end_packet);

        fclose(source_file);
    } else {
        // Rx: Receive file
        unsigned char *receive_buffer = malloc(MAX_IFRAME_SIZE);
        int received_len = -1;
        while ((received_len = llread(receive_buffer)) < 0); // Waits for start packet

        unsigned long received_file_size = 0;
        unsigned char *file_name = extractControlPacket(receive_buffer, received_len, &received_file_size); //extracts file size / name
        stats.file_size = received_file_size;

        //estimate total packets to receive
        int total_packets = (received_file_size + MAX_PAYLOAD_SIZE - 1) / MAX_PAYLOAD_SIZE;

        char *output_file_name = "penguin-received.gif";
        printf("Writing to file: %s\n", output_file_name);

        FILE *output_file = fopen(output_file_name, "wb+");
        if (!output_file) {
            fprintf(stderr, "Failed to create output file\n");
            free(file_name);
            free(receive_buffer);
            exit(-1);
        }
        //progress bar
        float last_progress = -1.0; //track last displayed progress
        printf("Receiving file:");

        while (1) {
            while ((received_len = llread(receive_buffer)) < 0);
            if (received_len == 0) break;
            if (receive_buffer[0] != 3) { // it's not an end packet (type 3)
                unsigned char *data_content = malloc(received_len - 4);
                extractDataPacket(receive_buffer, received_len, data_content);
                fwrite(data_content, sizeof(unsigned char), received_len - 4, output_file);
                free(data_content);
                stats.packets_received++;

                //calculate bar progress
                float progress = ((float)stats.packets_received / total_packets) * 100;
                if ((int)progress != (int)last_progress) {
                    display_progress_bar(progress, 50);
                    last_progress = progress; //increases
                }
            } else { // End packet
                break;
            }
        }
        display_progress_bar(100, 50);
        printf("\n");

        //printf("Cleaning up: file_name=%p, buffer=%p\n", (void*)file_name, (void*)receive_buffer);
        fclose(output_file);
        free(file_name);
        free(receive_buffer);
        //printf("Initiating llclose\n");
        llclose(link_fd);
        //printf("Completed llclose\n");
    }

    gettimeofday(&transfer_endtime, NULL);
    stats.transfer_time = (transfer_endtime.tv_sec - transfer_starttime.tv_sec) +
                           (transfer_endtime.tv_usec - transfer_starttime.tv_usec) / 1000000.0;
    stats.transfer_rate = stats.file_size / stats.transfer_time;

#ifdef LINK_LAYER_RETRANSMISSIONS
    stats.retransmissions = total_retransmissions;
#endif

    // Display transfer statistics
    if (connection.role == LlTx) printf("\n--- Tx: Transfer stats ---\n");
    if (connection.role == LlRx) printf("\n--- Rx: Receiver stats ---\n");
    printf("Total Bytes: %ld\n", stats.file_size);
    if (connection.role == LlTx) {
        printf("Packets Transmitted: %d\n", stats.packets_transmitted);
    } else {
        printf("Packets Collected: %d\n", stats.packets_received);
    }
    printf("Elapsed Time: %.3f seconds\n", stats.transfer_time);
    printf("Transfer Rate: %.2f bytes/second\n", stats.transfer_rate);
#ifdef LINK_LAYER_RETRANSMISSIONS
    if (connection.role == LlTx && stats.packets_transmitted > 0) {
        printf("Retransmissions: %d\n", stats.retransmissions);
        printf("Packet Loss Rate: %.2f%%\n", (stats.retransmissions / (float)stats.packets_transmitted) * 100); //packets retransmitted / packets transmitted
    }
#endif

    // efficiency S = R/C
    double R = stats.transfer_rate * 8; // bits/s
    double C = BAUDRATE; //(double)connection.baudRate; // bits/s
    double S = R / C;
    printf("Link Efficiency (S = R/C): %.4f%%\n", S * 100);

    if (connection.role == LlTx) {
        llclose(link_fd);
    }
}

// Extract file info from control packet
unsigned char *extractControlPacket(unsigned char *buffer, int buffer_len, unsigned long *size_out) {
    unsigned char size_bytes_count = buffer[2];
    unsigned char size_bytes[size_bytes_count];
    memcpy(size_bytes, buffer + 3, size_bytes_count);
    *size_out = 0;
    for (unsigned int idx = 0; idx < size_bytes_count; idx++) {
        *size_out |= (size_bytes[size_bytes_count - idx - 1] << (8 * idx));
    }

    unsigned char name_bytes_count = buffer[3 + size_bytes_count + 1];
    unsigned char *extracted_name = malloc(name_bytes_count + 1);
    memcpy(extracted_name, buffer + 3 + size_bytes_count + 2, name_bytes_count);
    extracted_name[name_bytes_count] = '\0';
    return extracted_name;
}

// Build a control packet for start/end
unsigned char *createControlPacket(unsigned int packet_type, const char *file_name, long file_length, unsigned int *packet_len) {
    int size_field_len = (int)ceil(log2f((float)file_length) / 8.0);
    int name_len = strlen(file_name);
    *packet_len = 1 + 2 + size_field_len + 2 + name_len;
    unsigned char *buffer = malloc(*packet_len);

    unsigned int position = 0;
    buffer[position++] = packet_type;
    buffer[position++] = 0; // File size type
    buffer[position++] = size_field_len;

    for (unsigned char idx = 0; idx < size_field_len; idx++) {
        buffer[2 + size_field_len - idx] = file_length & 0xFF;
        file_length >>= 8;
    }
    position += size_field_len;
    buffer[position++] = 1; // File name type
    buffer[position++] = name_len;
    memcpy(buffer + position, file_name, name_len);
    return buffer;
}

// Construct a data packet
unsigned char *buildDataPacket(unsigned char packet_seq, unsigned char *data_chunk, int chunk_len, int *total_len) {
    *total_len = 4 + chunk_len;
    unsigned char *buffer = malloc(*total_len);

    buffer[0] = 1; // Data packet type
    buffer[1] = packet_seq;
    buffer[2] = (chunk_len >> 8) & 0xFF;
    buffer[3] = chunk_len & 0xFF;
    memcpy(buffer + 4, data_chunk, chunk_len);

    return buffer;
}

// Read entire file content into memory
unsigned char *readFileContent(FILE *file, long file_length) {
    unsigned char *data_buffer = malloc(sizeof(unsigned char) * file_length);
    if (!data_buffer) {
        fprintf(stderr, "Memory allocation failed for file content\n");
        return NULL;
    }
    size_t bytes_read = fread(data_buffer, sizeof(unsigned char), file_length, file);
    if (bytes_read < (size_t)file_length) {
        printf("Warning: Not all bytes were read from the file\n");
    }
    return data_buffer;
}

// Extract data from a data packet
void extractDataPacket(const unsigned char *buffer, unsigned int buffer_len, unsigned char *data_out) {
    memcpy(data_out, buffer + 4, buffer_len - 4);
}

// Function to display a progress bar
void display_progress_bar(float progress, int bar_width) {
    int filled = (int)(progress * bar_width / 100.0); // Number of filled cells
    // will take the current progress as a % and print a bar
    int empty = bar_width - filled;

    printf("\r["); // Return to start of line. /r to write in the same line
    for (int i = 0; i < filled; i++) printf(">"); // fill bar
    for (int i = 0; i < empty; i++) printf(" ");
    printf("] %.0f%%", progress);
    fflush(stdout); // output bar updates immediately
}