// Link layer header.
// NOTE: This file must not be changed.

#ifndef _LINK_LAYER_H_
#define _LINK_LAYER_H_

typedef enum
{
    LlTx,
    LlRx,
} LinkLayerRole;

typedef struct
{
    char serialPort[50];
    LinkLayerRole role;
    int baudRate;
    int nRetransmissions;
    int timeout;
} LinkLayer;

// SIZE of maximum acceptable payload.
// Maximum number of bytes that application layer should send to link layer
#define MAX_PAYLOAD_SIZE 1000

// MISC
#define FALSE 0
#define TRUE 1

////////////////////////////////////////////////
// by user
////////////////////////////////////////////////
#define FLAG 0x7E
#define ESC 0x7D
#define A_TR 0x03 //sent by Tx or replies by Rx
#define A_RT 0x01  //sent by Rx or replies by Tx
#define C_SET 0x03
#define C_UA 0x07
#define C_DISC 0x0B
//flags RR0, RR1, REJ0, REJ1
#define C_RR0 = 0x05;
#define C_RR1 = 0x85;
#define C_REJ0 = 0x01;
#define C_REJ1 = 0x81;


typedef enum {
    START,
    FLAG_RCV, /*initial flag received*/
    A_RCV,
    C_RCV,
    BCC1_OK,
    DATA,
    ENDF
} State;

//S Frames Function
int sendSFrame(int fd, unsigned char A, unsigned char C){

    unsigned char buf_sf[5] = {FLAG, A, C, C ^ A, FLAG};

    return write(fd, buf_sf, 5);
}
/*types of sframes:
sendSFrame(fd, A_RT, C_UA); //UA
sendSFrame(fd, A_RT, C_RR0); //RR Ns0
sendSFrame(fd, A_RT, C_RR1); //RR Ns1
sendSFrame(fd, A_RT, C_REJ0);
sendSFrame(fd, A_RT, C_REJ1);
- DISC
*/

////////////////////////////////////////////////


// Open a connection using the "port" parameters defined in struct linkLayer.
// Return "1" on success or "-1" on error.
int llopen(LinkLayer connectionParameters);

// Send data in buf with size bufSize.
// Return number of chars written, or "-1" on error.
int llwrite(const unsigned char *buf, int bufSize);

// Receive data in packet.
// Return number of chars read, or "-1" on error.
int llread(unsigned char *packet);

// Close previously opened connection.
// if showStatistics == TRUE, link layer should print statistics in the console on close.
// Return "1" on success or "-1" on error.
int llclose(int showStatistics);

#endif // _LINK_LAYER_H_
