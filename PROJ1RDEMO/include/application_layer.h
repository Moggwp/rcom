// Application layer protocol header.
// NOTE: This file must not be changed.

#ifndef _APPLICATION_LAYER_H_
#define _APPLICATION_LAYER_H_

#include <stdio.h>

typedef struct {
    double transfer_time;      // Seconds
    long int file_size;        // Bytes
    int packets_transmitted;          // Number of data packets sent (Tx)
    int packets_received;      // Number of data packets received (Rx)
    int retransmissions;       // Number of retransmissions (requires link layer support)
    double transfer_rate;         // Bytes per second
} TransferStats;


// Main function to initiate file transfer (application layer).
//   port_name: Serial port identifier (e.g., /dev/ttyS0).
//   device_role: tx, rx.
//   BaudRateConnect: Baud rate for the serial connection.
//   max_attempts: Maximum frame retries.
//   timeout: Timeout for frame transmission (seconds).
//   file_path: Path to the file to send or receive/name.
void applicationLayer(const char *port_name, const char *device_role, int BaudRateConnect,
                       int max_attempts, int timeout, const char *file_path);

// Extracts file information from a control packet (name and size)
//   buffer: The control packet data.
//   buffer_len: Length of the packet.
//   size_out: Pointer to store the extracted file size.
// Returns: Pointer to the extracted file name (freed by caller).
unsigned char *extractControlPacket(unsigned char *buffer, int buffer_len, unsigned long *size_out);

// Extracts data from a data packet.
//   buffer: The data packet.
//   data_out: Buffer to store the extracted data.
void extractDataPacket(const unsigned char *buffer, unsigned int buffer_len, unsigned char *data_out);

// Creates a control packet for start or end of transfer.
//   packet_type: Type of control packet (2 for start, 3 for end).
//   file_length: Size of the file in bytes.
//   packet_len: Pointer to store the length of the created packet.
// Returns: Pointer to the created packet (freed by caller).
unsigned char *createControlPacket(unsigned int packet_type, const char *file_name, long file_length, unsigned int *packet_len);

// Builds a data packet for transmission.
//   packet_seq: Sequence number of the packet.
//   data_chunk: Data to include in the packet.
//   chunk_len: Length of the data.
//   total_len: Pointer to store the total length of the packet.
// Returns: Pointer to the created packet (freed by caller).
unsigned char *buildDataPacket(unsigned char packet_seq, unsigned char *data_chunk, int chunk_len, int *total_len);

// Reads the entire content of a file into memory.
// Arguments:
//   file: File pointer to read from.
//   file_length: Size of the file in bytes.
// Returns: Pointer to the file content (freed by caller).
unsigned char *readFileContent(FILE *file, long file_length);

//Progress bar tracking:
//Tx: showing packets sent
//Rx: showing packets received
//   progress: Progress % (0-100)
//   bar_width: Width of the progress bar
void display_progress_bar(float progress, int bar_width);

#endif // _APPLICATION_LAYER_H_