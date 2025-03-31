// Link layer protocol implementation

#include "link_layer.h" // connectionParameters

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#define MAX_IFRAME_SIZE 2048  // valor seguro e alto
// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source

//to be accessed by all functions
int fd; 
static LinkLayerRole role;


//////////////int fd; 
//////////////////////////////
//Tx
volatile int alarmEnabled = FALSE;
int alarmCount = 0;
int timeout = 0; //timeout = connectionParameters.timeout
int max_retransmissions = 0;//retrans = connectionParameters.nRetransmissions
int total_retransmissions = 0;
int ua_received = 0;
unsigned char next_NsTx = C_NS1; //start, first one will be C_NS0
unsigned char C_Byte, C_ByteRx = 0x55, finishedC = 0x55; //random
unsigned char byte, bcc2, bcc2xor = 0, last_Ns = 0x55; //inicializar last_Ns com valor à toa
unsigned char C_received = 0x55;
int data_count = 0;


typedef enum {
    START,
    FLAG_RCV, //initial flag received
    A_RCV,
    C_RCV,
    BCC1,
    DATA,
    DESTUFF_ESC,
    ENDF
} State;

State ActualState = START; 

// user-defined function to handle alarms (handler function)
void alarmHandler(int signal)
{ //this function will be called when the alarm is triggered
    alarmEnabled = TRUE; // can be used to change a flag that increases the number of alarms
    alarmCount++;
    printf("Alarm #%d\n", alarmCount); // no response, retransmitting SET
}

////////////////////
///functions used
int connection (const char *serialPort){
        // Open serial port device for reading and writing, and not as controlling tty
        // because we don't want to get killed if linenoise sends CTRL-C.
        fd = open(serialPort, O_RDWR | O_NOCTTY);
    
        if (fd < 0)
        {
            perror(serialPort);
            exit(-1);
        }
    
        struct termios oldtio;
        struct termios newtio;
    
        // Save current port settings
        if (tcgetattr(fd, &oldtio) == -1)
        {
            perror("tcgetattr");
            exit(-1);
        }
    
        // Clear struct for new port settings
        memset(&newtio, 0, sizeof(newtio));
    
        newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
        newtio.c_iflag = IGNPAR;
        newtio.c_oflag = 0;
    
        // Set input mode (non-canonical, no echo,...)
        newtio.c_lflag = 0;
        newtio.c_cc[VTIME] = 1; // Inter-character timer
        newtio.c_cc[VMIN] = 0;  // Blocking read until X chars received
    
        // VTIME e VMIN should be changed in order to protect with a
        // timeout the reception of the following character(s)
    
        // Now clean the line and activate the settings for the port
        // tcflush() discards data written to the object referred to
        // by fd but not transmitted, or data received but not read,
        // depending on the value of queue_selector:
        //   TCIFLUSH - flushes data received but not read.
        tcflush(fd, TCIOFLUSH);
    
        // Set new port settings
        if (tcsetattr(fd, TCSANOW, &newtio) == -1)
        {
            perror("tcsetattr");
            exit(-1);
        }
    
        printf("New termios structure set\n");

        return fd; 
}


////////////////////////////////////////////////
// LLOPEN, Tx: send SET // Rx: send UA
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters) // param. given by app layer
{
    
    fd = connection(connectionParameters.serialPort); //file descriptor of serialPort
    timeout = connectionParameters.timeout;
    max_retransmissions = connectionParameters.nRetransmissions;

    if (fd < 0){
        return -1; //error opening serial port
    }
    role = connectionParameters.role; //to use in llclose, for each process LlTx or LlRx

    switch (connectionParameters.role){
        case LlTx: { //send SET to Rx

            (void) signal(SIGALRM, alarmHandler);

            while (ua_received == 0 && total_retransmissions <= max_retransmissions){ //nRetransmissions left to send SET Frame (within timeout period ofc)
                sendSFrame(fd, A_TR, C_SET); //send SET

                alarm(connectionParameters.timeout);
                alarmEnabled = FALSE;
                readCFrameTx(fd); //read UA
                if (ua_received == 0){
                    total_retransmissions++;
                    continue;
                }
            }
            break;
           }


           case LlRx: {
            //receive SET, send UA
                    C_received = readCFrameRx(fd); //loop inside readCFrameRx
                    //printf("llopen: Rx received C = 0x%02X\n", C_received);
                    if (C_received == C_SET){ //if SET was read
                        sendSFrame(fd, A_RT, C_UA); //send UA
                    }
                    else{
                        printf("Error receiving SET\n");
                        return -1; //error
                    }
            }
           break;
            }
        return fd;
}
     

////////////////////////////////////////////////
// LLWRITE I frames from Tx
//implement alarmCount < retransmissions or something like that
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize) {
    if (buf == NULL) return -1;

    tcflush(fd, TCIFLUSH);
    ActualState = START;
    alarmCount = 0;

    while (total_retransmissions <= max_retransmissions) {
        //printf("llwrite: Sending IFrame...\n");
        ActualState = START;
        int bytesI_sent = sendIFrame(fd, buf, bufSize);
        if (bytesI_sent < 0){
            //printf("llwrite: sendIFrame failed, bufsize = %d\n", bufSize);
            return -1;
        } 

        alarm(timeout);
        alarmEnabled = FALSE;
        unsigned char CFlagRcv = readCFrameTx(fd);
        //printf("Tx reads RR/REJ... CFlagRcv: 0x%02X\n", CFlagRcv);
        //sleep(2);

        if (ActualState == ENDF) { //full frame received
        
            if (CFlagRcv == C_RR0) {
                alarm(0);
                next_NsTx = C_NS0;
                return bufSize; // Success
            } else if (CFlagRcv == C_RR1) {
                alarm(0);
                next_NsTx = C_NS1;
                return bufSize; // Success
            } else if (CFlagRcv == C_REJ0) {
                alarm(0);
                next_NsTx = C_NS0;
                total_retransmissions++; //retrans
                continue;
            } else if (CFlagRcv == C_REJ1) {
                alarm(0);
                next_NsTx = C_NS1;
                total_retransmissions++; //retrans
                continue;
            }
        }
        else if (alarmEnabled == TRUE && ActualState != ENDF) {
            printf("llwrite: Timeout occurred, retransmitting...\n");
            total_retransmissions++; // Timeout occurred
            continue; //to next loop iteration
        }
    }
    if (total_retransmissions > max_retransmissions){
		printf("llwrite: Max retransmissions reached, %d\n", max_retransmissions);
		return -1;
	}
	return -1;

}

////////////////////////////////////////////////
// LLREAD: Rx Reading I frames...
////////////////////////////////////////////////
int llread(unsigned char *packet) {
    unsigned char byte, cField;
    int i = 0;
    ActualState = START;

    while (ActualState != ENDF) {
        if (read(fd, &byte, 1) > 0) {
            switch (ActualState) {
                case START:
                    if (byte == FLAG) ActualState = FLAG_RCV;
                    break;
                case FLAG_RCV:
                    //sleep(1);
                    if (byte == A_TR) ActualState = A_RCV;
                    else if (byte != FLAG) ActualState = START;
                    break;
                case A_RCV:
                    if (byte == C_NS0 || byte == C_NS1) {
                        cField = byte;
                        ActualState = C_RCV;
                    } else if (byte == C_DISC) {
                        sendSFrame(fd, A_RT, C_DISC);
                        printf("llread: DISC\n");
                        return 0;
                    } else if (byte == FLAG) ActualState = FLAG_RCV;
                    else ActualState = START;
                    break;
                case C_RCV:
                    //sleep(1);
                    if (byte == (A_TR ^ cField)) ActualState = DATA;
                    else if (byte == FLAG) ActualState = FLAG_RCV;
                    else ActualState = START;
                    break;
                case DATA:
                    if (byte == ESC) ActualState = DESTUFF_ESC;
                    else if (byte == FLAG) {
                        unsigned char bcc2 = packet[i - 1];
                        i--; // Remove BCC2 from data
                        unsigned char bcc2xor = packet[0];
                        for (int j = 1; j < i; j++) bcc2xor ^= packet[j];

                        if (bcc2 == bcc2xor) {
                            if (cField == last_Ns) {
                                printf("llread: Duplicate frame\n");
                                //don't update last_Ns. updating would incorrectly mark this frame as a new frame.
                                sendSFrame(fd, A_RT, cField == C_NS0 ? C_RR1 : C_RR0); //if C == C_NS0 is TRUE, then C = C_RR1. if FALSE, C = C_RR0
                                return -1;
                            } else {
                                last_Ns = cField;
                                sendSFrame(fd, A_RT, cField == C_NS0 ? C_RR1 : C_RR0);
                                ActualState = ENDF;
                                return i; // Payload size only
                            }
                        } else {
                            printf("llread: BCC2 error\n");
                            sendSFrame(fd, A_RT, cField == C_NS0 ? C_REJ0 : C_REJ1);
                            return -1;
                        }
                    } else {
                        if (i >= MAX_IFRAME_SIZE){
                            printf("llread: Buffer overflow when destuffing, i = %d\n", i);
                            return -1;
                        }
                        packet[i++] = byte;
                    }
                    break;
                case DESTUFF_ESC: //the next byte shows if it's FLAG or ESC
                    ActualState = DATA;
                    if (byte == 0x5E) packet[i++] = FLAG;
                    else if (byte == 0x5D) packet[i++] = ESC;
                    else {
                        packet[i++] = byte ^ 0x20; //FLAG / ESC xor 0x20
                    }
                    break;
                default:
                    break;
            }
        }
    }
    return -1;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics) {
    ActualState = START;
 
    (void) signal(SIGALRM, alarmHandler);
    //printf("llclose from DLL\n");

    if (role == LlRx) {
        // Rx: Receiving DISC from Tx
        while (total_retransmissions <= max_retransmissions && ActualState != ENDF) {
            alarm(timeout);
            alarmEnabled = FALSE;
            
            unsigned char c = readCFrameRx(fd);
            if (c == C_DISC && ActualState == ENDF){
                alarm(0);
                break;
            }
            alarm(0);
            total_retransmissions++;
        }
        if (ActualState != ENDF) {
            printf("llclose: Rx failed to receive DISC from Tx\n");
            return -1;
        }
        sendSFrame(fd, A_RT, C_DISC);
        //printf("llclose sending RT DISC\n");  
   
        //Rx: receiving UA from Tx
        ActualState = START;
        
        while (total_retransmissions <= max_retransmissions && ActualState != ENDF) {
            alarm(timeout);
            alarmEnabled = FALSE;
            unsigned char c = readCFrameRx(fd);
            if (c == C_UA && ActualState == ENDF){
                alarm(0);
                break;
            }
            alarm(0);
            total_retransmissions++;
        }
        if (ActualState != ENDF) {
            printf("llclose: Rx failed to receive UA from Tx\n");
            return -1;
        }
    }
    else {  // LlTx
        // Tx: Send DISC first
        while (total_retransmissions <= max_retransmissions && ActualState != ENDF) {
            sendSFrame(fd, A_TR, C_DISC);
            //printf("llclose sending 1st TR DISC\n");
            alarm(timeout);
            alarmEnabled = FALSE;

            unsigned char c = readCFrameTx(fd);
            if (c == C_DISC && ActualState == ENDF){
                alarm(0);
                break;
            }
            alarm(0);
            total_retransmissions++;
        }

        if (ActualState != ENDF) {
            printf("llclose: Tx failed to receive DISC from Rx\n");
            return -1;
        }

        sendSFrame(fd, A_TR, C_UA); //Tx: disc was received, send UA
    }

    //printf("DLL closing\n");
    return close(fd) >= 0 ? 1 : -1;  // Per spec: 1 on success, -1 on error
}


//S Frames Function
int sendSFrame(int fd, unsigned char A, unsigned char C){
    //printf("Sending S Frame with C = 0x%02X\n", C);
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


// waiting for Rx response, UA or RR/REJ
unsigned char readCFrameTx(int fd){
    while (!alarmEnabled && ActualState != ENDF) {
        int bytes_readUA = read(fd, &byte, 1);  // Reading UA
        //printf("Tx Byte read: 0x%02X\n", byte);

        // State Machine for the byte (UA from Rx)
        if (bytes_readUA > 0) {
            switch (ActualState){
                case START:
                    if (byte == FLAG){
                        ActualState = FLAG_RCV;
                        //printf("FLAG received\n");
                    }
                    break;

                case FLAG_RCV:
                    if (byte == A_RT){
                        ActualState = A_RCV;
                        //printf("A received\n");
                    }
                    else if (byte != FLAG){ //if flag waits here
                        ActualState = START;
                    }
                    break;

                case A_RCV:
                    if (byte == C_UA || byte == C_RR0 || byte == C_RR1 || byte == C_REJ0 || byte == C_REJ1 || byte == C_DISC){
                        C_Byte = byte;
                        ActualState = C_RCV;
                        //printf("C received\n");
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
                        //printf("BCC1 received\n");
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
                        if (C_Byte == C_UA){ //new
                            ua_received = 1; 
                        }
                        //printf("C frame received correctly.\n");
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
    while (!alarmEnabled && ActualState != ENDF) { 
        int bytes_readUA = read(fd, &byte, 1);  // Reading UA
        //printf("Rx Byte read: 0x%02X\n", byte);  
        // State Machine for the byte (SET from Tx)
        if (bytes_readUA > 0) {
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
                        //printf("C frame received correctly...\n");
                        //send UA de0bug
                        if (C_ByteRx == C_SET){
                            
                            //printf("Sent UA!\n");
                            sendSFrame(fd, A_RT, C_UA);  
                        }
                        else if (C_ByteRx == C_DISC){
                            sendSFrame(fd, A_RT, C_DISC);
                        }
                        else if (C_ByteRx == C_UA){
                            //printf("DISC is over\n");
                        }
                        
                        finishedC = C_ByteRx;
                        ActualState = ENDF;
                    }
                    else{
                        ActualState = START; //error, not final flag
                    }
                    break;

                default:
                    ActualState = START; //not sure if it's needed

            }
            //return finishedC;
        }
    }
    
    return finishedC;
}



int sendIFrame(int fd, const unsigned char *data, int dataSize) {
    if (dataSize <= 0 || dataSize > MAX_IFRAME_SIZE){
        printf("sendIFrame: Invalid dataSize = %d\n", dataSize);
        return -1;
    }
    if (data == NULL) {
        printf("sendIFrame: data is NULL\n");
        return -1;
    }


    unsigned char *Iframe = (unsigned char*)malloc(MAX_IFRAME_SIZE);
    if (!Iframe){
        printf("sendIFrame: malloc failed\n");
        return -1;
    }
    int Index = 0;

    Iframe[Index++] = FLAG;
    Iframe[Index++] = A_TR;
    Iframe[Index++] = next_NsTx;
    Iframe[Index++] = A_TR ^ next_NsTx;

    unsigned char BCC2 = data[0];
    for (int i = 1; i < dataSize; i++) BCC2 ^= data[i];

    for (int i = 0; i < dataSize; i++) {
        if (data[i] == FLAG) {
            Iframe[Index++] = ESC;
            Iframe[Index++] = 0x5E;
        } else if (data[i] == ESC) {
            Iframe[Index++] = ESC;
            Iframe[Index++] = 0x5D;
        } else {
            Iframe[Index++] = data[i];
        }
        if (Index >= MAX_IFRAME_SIZE - 3) return -1;
    }

    if (BCC2 == FLAG) {
        Iframe[Index++] = ESC;
        Iframe[Index++] = 0x5E;
    } else if (BCC2 == ESC) {
        Iframe[Index++] = ESC;
        Iframe[Index++] = 0x5D;
    } else {
        Iframe[Index++] = BCC2;
    }

    Iframe[Index++] = FLAG;
    
    if (write(fd, Iframe, Index) < 0) return -1;
    free (Iframe);

    return Index;
}
