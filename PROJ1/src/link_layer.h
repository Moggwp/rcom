// Link layer header.

#ifndef _LINK_LAYER_H_
#define _LINK_LAYER_H_
#include <signal.h> //added
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#define _POSIX_SOURCE 1
#define BAUDRATE 38400
// SIZE of maximum acceptable payload.
// Maximum number of bytes that application layer should send to link layer
#define MAX_PAYLOAD_SIZE 1000

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

// MISC
#define FALSE 0
#define TRUE 1

////////////////////////////////////////////////
// by user
////////////////////////////////////////////////
#define BUF_SIZE 500
#define FLAG 0x7E
#define ESC 0x7D
#define A_TR 0x03 //sent by Tx or replies by Rx
#define A_RT 0x01  //sent by Rx or replies by Tx
#define C_SET 0x03
#define C_UA 0x07
#define C_DISC 0x0B
#define C_NS0 0x00
#define C_NS1 0x40 //not sure
//flags RR0, RR1, REJ0, REJ1
#define C_RR0 0x05
#define C_RR1 0x85
#define C_REJ0 0x01
#define C_REJ1 0x81


typedef enum {
    START,
    FLAG_RCV, /*initial flag received*/
    A_RCV,
    C_RCV,
    BCC1,
    DATA,
    ENDF
} State;

State ActualState = START;
//Tx





// user-defined function to handle alarms (handler function)
void alarmHandler(int signal)
{ //this function will be called when the alarm is triggered
    alarmEnabled = TRUE; // can be used to change a flag that increases the number of alarms
    alarmCount++;
    printf("Alarm #%d\n", alarmCount); // no response, retransmitting SET
}

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
// install the function signal to be automatically
//invoked when the timer expires, invoking alarmHandler
//(void) signal(SIGALRM, alarmHandler);
//Enviar o quadro SET

// waiting for Rx response
unsigned char readCFrameTx(int fd){
    while (!alarmEnabled && ActualState != ENDF) { //ua_received pd ser o llopen case llTx
        int bytes_readUA = read(fd, &byte, 1);  // Reading UA
        printf("Byte read: 0x%02X\n", byte);  // Depuração

        // State Machine for the byte (UA from Rx)
        if (bytes_readUA > 0) {
            switch (ActualState){
                case START:
                    if (byte == FLAG){
                        ActualState = FLAG_RCV;
                        printf("FLAG received\n");
                    }
                    break;

                case FLAG_RCV:
                    if (byte == A_RT){
                        ActualState = A_RCV;
                        printf("A received\n");
                    }
                    else if (byte != FLAG){ //if flag waits here
                        ActualState = START;
                    }
                    break;

                case A_RCV:
                    if (byte == C_UA || byte == C_RR0 || byte == C_RR1 || byte == C_REJ0 || byte == C_REJ1 || byte == C_DISC){
                        C_Byte = byte;
                        ActualState = C_RCV;
                        printf("C received\n");
                    }
                    else if (byte == FLAG){
                        ActualState = FLAG_RCV;
                    }
                    else {
                        ActualState = START;
                    }
                    break;

                case C_RCV:
                    if (byte == (A_RT ^ C_UA) || byte == (A_RT ^ C_RR0) || byte == (A_RT ^ C_RR1) || byte == (A_RT ^ C_REJ0) || byte == (A_RT ^ C_REJ1) || byte == (A_RT ^ C_DISC)){
                        ActualState = BCC1;
                        printf("BCC1 received\n");
                    }
                    else if (byte == FLAG){
                        ActualState = FLAG_RCV;
                    }
                    else {
                        ActualState = START;
                    }
                    break;

                case BCC1:
                    if (byte == FLAG){
                        ActualState = ENDF;
                        STOP = TRUE;
                        ua_received = 1;
                        printf("C frame received correctly.\n");
                        alarm(0);
                    }
                    else{
                        ActualState = START;
                    }
                    break;

                default:
                    ActualState = START;
                    break;
            }
        }
    }
    return C_Byte;
}

unsigned char readCFrameRx(int fd){
        
    switch (ActualState) {
        case START:
            if(byte == FLAG){//flag inicial
                ActualState = FLAG_RCV;
            }
            break;
        
        case FLAG_RCV: //stuck in FLAG_RCV if byte == FLAG
            if (byte == A_TR){
                ActualState = A_RCV;
            }
            else if (byte != FLAG){ //Other_RCV
                ActualState = START;
            }
            break;

        case A_RCV:
            if (byte == C_SET || byte == C_DISC || byte == C_UA){
                C_ByteRx = byte;
                ActualState = C_RCV;
            }
            else if (byte == FLAG){//flag inicial, é pq é p comecar dnv
                ActualState = FLAG_RCV; //back to FLAG_RCV
            }
            else{ //Other_RCV
                ActualState = START;
            }
            break;
        
        case C_RCV:
            if (byte == (A_TR ^ C_ByteRx)) 
                ActualState = BCC1;
            else if (byte == FLAG) 
                ActualState = FLAG_RCV;
            else
                ActualState = START;
            break;
        
        case BCC1: //last one
            if (byte == FLAG){
                printf("C frame received correctly...\n");
                //send UA de0bug
                if (C_ByteRx == C_SET){
                    
                    printf("Sent UA!\n");
                    sendSFrame(fd, A_RT, C_UA);  
                }
                else if (C_ByteRx == C_DISC){
                    sendSFrame(fd, A_RT, C_DISC);
                }
                else if (C_ByteRx == C_UA){
                    printf("DISC is over\n");
                }
                
                ActualState = START; //start of the next state machine, I frames
                finishedC = C_ByteRx;
            }
            else{
                ActualState = START; //error, not final flag
            }
            break;

        default:
            ActualState = START; //not sure if it's needed

    }
    return finishedC;
}



int sendIFrame(int fd, const unsigned char *data, int dataSize){
    int IframeSize = dataSize + 6; /*dataSize + 6 bytes 
    header(4): FLAG, A, C, BCC1. trailer(2): BCC2, FLAG */

    //allocate memory for the I frame
    unsigned char *Iframe = malloc(IframeSize);
    if (!Iframe ){
        printf("ERROR: malloc failed\n");
        return -1;
    }
    //Header:
    Iframe[0] = FLAG;
    Iframe[1] = A_TR;
    printf("NsTx in IFrame: 0x%02X\n", next_NsTx);
    Iframe[2] = next_NsTx; //C_NS0 or C_NS1
    //Iframe[2] = 0x40; //C, Ns
    Iframe[3] = Iframe[1] ^ Iframe[2]; //BCC1

    //copy from source (data) to destination (Iframe)
    memcpy(&Iframe[4], data, dataSize); //&Iframe[4] or Iframe + 4

    // calculate BCC2 before byte stuffing
    unsigned char BCC2 = data[0];
    for (int i = 1; i < dataSize; i++){
        BCC2 ^= data[i];
    }


    //data byte stuffing
    int Index = 4; //DataIndex
    for (int i = 0; i < dataSize; i++){
        
        switch(data[i]){
        
            case FLAG:
                IframeSize += 1;
                unsigned char *temp = realloc(Iframe, IframeSize);
                if (!temp) {
                    free(Iframe);  // free  original memory before error
                    printf("ERROR: realloc failed\n");
                    return -1;
                }

                Iframe = temp;  // updates Iframe if realloc() was succeeded

                Iframe[Index++] = ESC; //writes ESC in Index, then increments Index
                Iframe[Index++] = 0x5E; // FLAG ^ 0x20 in Index incremented, and increments again to the next one
                break;

            case ESC:
                IframeSize += 1;
                unsigned char *temp2 = realloc(Iframe, IframeSize);
                if (!temp2) {
                    free(Iframe);  // free  original memory before error
                    printf("ERROR: realloc failed\n");
                    return -1;
                }
                Iframe = temp2;  // updates Iframe if realloc() was succeeded

                Iframe[Index++] = ESC;
                Iframe[Index++] = 0x5D; // ESC ^ 0x20
                break;


            default:
                Iframe[Index++] = data[i];
                break;
        }


    //trailer:
    //if BCC2 is FLAG or ESC, byte stuffing is needed
    if (BCC2 == FLAG || BCC2 == ESC){
        IframeSize += 1;
        unsigned char *temp = realloc(Iframe, IframeSize);
        if (!temp) {
            free(Iframe);  // free  original memory before error
            printf("ERROR: realloc failed\n");
            return -1;
        }

        Iframe = temp;  // updates Iframe if realloc() was succeeded

        Iframe[Index++] = ESC; 
        Iframe[Index++] = BCC2 ^ 0x20; //FLAG ^ 0x20 || ESC ^ 0x20, depends on BCC2 value
        //so works both cases
    }
    else{
        Iframe[Index++] = BCC2; //byte stuffing not needed
    }    
    Iframe[Index++] = FLAG;

        // sends Iframe to serial port
        if (write(fd, Iframe, IframeSize) < 0) {
            free(Iframe);
            printf("ERROR: Error writing Iframe\n");
            return -1;
        }
    
        free(Iframe);
        return IframeSize;
    }
    return IframeSize;
}








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
