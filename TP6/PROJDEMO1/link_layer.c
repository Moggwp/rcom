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

int fd; 

unsigned char C_read;

//////////////int fd; 
//////////////////////////////
//Tx
volatile int STOP = FALSE;
volatile int alarmEnabled = FALSE;
volatile int retry = FALSE; //added by me
int alarmCount = 0;
int timeout = 3; //timeout = connectionParameters.timeout
int retrans = 3; //retrans = connectionParameters.nRetransmissions
int ua_received = 0;
int ACK_received = 0;
unsigned char C_Byte = 0x55; //random
unsigned char next_NsTx = C_NS1; //start, first one will be C_NS0
unsigned char CFlagRcv = 0;
unsigned char byte;
unsigned char C_ByteRx = 0x55, finishedC = 0x55; //random
unsigned char byte, bcc2, bcc2xor = 0, last_Ns = 0x55; //inicializar last_Ns com valor à toa
unsigned char C_received = 0x55;
int data_count = 0;
volatile int duplicated = FALSE; 

typedef enum {
    START,
    FLAG_RCV, /*initial flag received*/
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
//var definition
//int alarmEnabled = FALSE;
//int alarmCount = 0;
/*timeout = connectionParameters.timeout;
    retrans = connectionParameters.nRetransmissions;*/


////////////////////
///functions used
int connection (const char *serialPort){
        // Program usage: Uses either COM1 or COM2
        /*const char *serialPortName = argv[1];

        if (argc < 2)
        {
            printf("Incorrect program usage\n"
                   "Usage: %s <SerialPort>\n"
                   "Example: %s /dev/ttyS1\n",
                   argv[0],
                   argv[0]);
            exit(1);
        } */
    
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
// LLOPEN, Tx: send SET and Rx: receive UA
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters) // param. given by app layer
{
    fd = connection(connectionParameters.serialPort); //file descriptor of serialPort
    if (fd < 0){
        return -1; //error opening serial port
    }

    switch (connectionParameters.role){
        case LlTx: { //send SET to Rx

            (void) signal(SIGALRM, alarmHandler);

            while (ua_received == 0 && connectionParameters.nRetransmissions != 0){ //nRetransmissions left to send SET Frame (within timeout period ofc)
                sendSFrame(fd, A_TR, C_SET); //send SET

                alarm(connectionParameters.timeout);
                alarmEnabled = FALSE;
                //wait for UA
                readCFrameTx(fd); //read UA
                //what's going to happen with this?
                connectionParameters.nRetransmissions--; //decrement remaining retransmissions

            }
            break;
           }


           case LlRx: {
            //receive SET, send UA
                //read byte by byte   
                //int bytes_read = read(fd, &byte, 1); //return: num bytes == 1
                //if (bytes_read > 0){

                    C_received = readCFrameRx(fd); //loop inside readCFrameRx
                    printf("llopen: Rx received C = 0x%02X\n", C_received);
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
int llwrite(const unsigned char *buf, int bufSize)
{
    // TODO
    if (buf == NULL){
        return -1; //error, buf is NULL
    }

    //int bytesI_sent = sendIFrame(fd, buf, bufSize);

    //printf("Iframe sent\n"); //debug

    // Limpar resposta antiga (UA que ficou no buffer)
    //llwrite envia I frame, entra em loop de resposta
    //ele estava com UA antiga no buffer, entao fez-se isto
    tcflush(fd, TCIFLUSH);
    
    ActualState = START;
    
    int FrameRejected = 0;
    int FrameAccepted = 0;

    while (alarmCount < retrans && FrameAccepted == 0){
        printf("llwrite: Sending IFrame...\n");

        int bytesI_sent = sendIFrame(fd, buf, bufSize);
        if (bytesI_sent < 0){
            printf("Error sending Iframe\n");  
            return -1;
        }

        //Flag from Receiver
        alarm(timeout);  //alarmHandler is called after timeout
        alarmEnabled = FALSE;

        //int FrameRejected = 0;
        
        
        CFlagRcv = readCFrameTx(fd); //read RR or REJ
        printf("Tx reads RR/REJ... CFlagRcv: 0x%02X\n", CFlagRcv);
        

        if (CFlagRcv ==  C_RR0 || CFlagRcv == C_REJ0){
            alarm(0);
            next_NsTx = C_NS0;
            printf("next_NsTx 0: 0x%02X\n", next_NsTx);
            if (CFlagRcv == C_REJ0){
                FrameRejected = 1;
            }
            else if (CFlagRcv == C_RR0){
                FrameAccepted = 1;
            }

        }
        else if (CFlagRcv == C_RR1 || CFlagRcv == C_REJ1){
            alarm(0);
            next_NsTx = C_NS1;
            printf("next_NsTx 1: 0x%02X\n", next_NsTx);
            if(CFlagRcv == C_REJ1){
                FrameRejected = 1;
            }
            else if(CFlagRcv == C_RR1){
                FrameAccepted = 1;
            }
        }
        if (FrameRejected == 1){
            return bytesI_sent; //not sure
        }
        /*else{ //Frame not rejected
            llclose(fd);
            return -1;
        }*/
        /*else if(CFlagRcv == C_DISC){
            printf("DISC received\n");
            sendSFrame(fd, A_TR, C_UA); //send UA back to stop connection
            STOP = TRUE;
        }*/
        
    }
    return 0;
}

////////////////////////////////////////////////
// LLREAD: Rx Reading I frames...
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    //packet vai ser a minha antiga data_buf
    // TODO

    //loop for reading

    unsigned char byte, cField;
    int i = 0;
    State ActualState = START;

    while (ActualState != ENDF) {  
        if (read(fd, &byte, 1) > 0) {
            switch (ActualState) {
                case START:
                    if (byte == FLAG) ActualState = FLAG_RCV;
                    break;
                case FLAG_RCV:
                    if (byte == A_TR) ActualState = A_RCV;
                    else if (byte != FLAG) ActualState = START;
                    break;
                case A_RCV:
                    if (byte == C_NS0 || byte == C_NS1){ //e' igual ao anterior ou nao?
                        ActualState = C_RCV;
                        cField = byte;   
                    }
                    else if (byte == FLAG) ActualState = FLAG_RCV;
                    else if (byte == C_DISC) {
                        sendSFrame(fd, A_RT, C_DISC);
                        printf("llread: DISC\n");
                        return 0;
                    }
                    else ActualState = START;
                    break;
                case C_RCV:
                    if (byte == (A_TR ^ cField)) ActualState = DATA;
                    else if (byte == FLAG) ActualState = FLAG_RCV;
                    else ActualState = START;
                    break;
                case DATA:
                    if (byte == ESC) ActualState = DESTUFF_ESC; //destuff
                    else if (byte == FLAG){
                        unsigned char bcc2 = packet[i-1];
                        i--;
                        packet[i] = '\0';
                        unsigned char bcc2xor = packet[0];

                        for (unsigned int j = 1; j < i; j++)
                            bcc2xor ^= packet[j];

                        if (bcc2 == bcc2xor){
                            printf("llread: right BCC2\n");
                            
                            ActualState = ENDF;
                            //duplicada ou nao?
                            if (cField == last_Ns){ //duplicado
                                printf("llread: cField duplicado\n");
                                if (cField == C_NS0) sendSFrame(fd, A_RT, C_RR1);
                                if (cField == C_NS1) sendSFrame(fd,A_RT, C_RR0);

                                return 0;
                            }
                            else{ //nao duplicado
                                printf("llread: cField nao duplicado\n");
                                last_Ns = cField;
                                if (cField == C_NS0) sendSFrame(fd, A_RT, C_RR1);
                                if (cField == C_NS1) sendSFrame(fd,A_RT, C_RR0);

                                return i;
                            }
                            
                        }
                        else{
                            printf("llread: Error: retransmission\n");
                            if (cField == C_NS0) sendSFrame(fd, A_RT, C_REJ0);
                            if (cField == C_NS1) sendSFrame(fd, A_RT, C_REJ1);
                            
                            return -1;
                        };

                    }
                    else{
                        packet[i++] = byte;
                    }
                    break;
                case DESTUFF_ESC:
                    ActualState = DATA;
                    if (byte == ESC || byte == FLAG) packet[i++] = byte;
                    else{
                        packet[i++] = ESC;
                        packet[i++] = byte;
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
int llclose(int showStatistics)
{
    // TODO
    //retrans e' o num max de retransm. por frame
    /*(void) signal(SIGALRM, alarmHandler);
    int try = 0;
    sendSFrame(fd, A_TR, C_DISC); //Tx send DISC
    alarm(timeout);  //alarmHandler is called after timeout
    alarmEnabled = FALSE;
    ActualState = START;
    C_read = readCFrameRx(fd); //Rx read DISC
    
    while (try < retrans && C_read != C_DISC){
        sendSFrame(fd, A_TR, C_DISC); //Tx send DISC

        alarm(timeout);  //alarmHandler is called after timeout
        alarmEnabled = FALSE;

        //ActualState = START;
        C_read = readCFrameTx(fd); //Rx read DISC
        alarm(0);
        try++;

        if(C_read == C_DISC){
            printf("DISC received\n");
            //sendSFrame(fd, A_TR, C_UA); //Rx returns UA to stop connection
            showStatistics = TRUE;
        }
        else{
          printf("Error receiving DISC. Next try\n");  
          try++;
        }
    }
    
    if (C_read != C_DISC){ //max tries reached
        printf("Max tries reached. Closing connection\n");
        return -1;
    }
    printf("Sending UA to close connection\n");
    sendSFrame(fd, A_TR, C_UA); //Rx returns UA to stop connection
    

    return close(fd);*/

    ActualState = START;
    unsigned char byte;
    (void) signal(SIGALRM, alarmHandler);
    
    while (retrans != 0 && ActualState != ENDF) {
                
        sendSFrame(fd, A_TR, C_DISC);
        alarm(timeout);
        alarmEnabled = FALSE;
                
        while (alarmEnabled == FALSE && ActualState != ENDF) {
            if (read(fd, &byte, 1) > 0) {
                switch (ActualState) {
                    case START:
                        if (byte == FLAG) ActualState = FLAG_RCV;
                        break;
                    case FLAG_RCV:
                        if (byte == A_RT) ActualState = A_RCV;
                        else if (byte != FLAG) ActualState = START;
                        break;
                    case A_RCV:
                        if (byte == C_DISC) ActualState = C_RCV;
                        else if (byte == FLAG) ActualState = FLAG_RCV;
                        else ActualState = START;
                        break;
                    case C_RCV:
                        if (byte == (A_RT ^ C_DISC)) ActualState = BCC1;
                        else if (byte == FLAG) ActualState = FLAG_RCV;
                        else ActualState = START;
                        break;
                    case BCC1:
                        if (byte == FLAG) ActualState = ENDF;
                        else ActualState = START;
                        break;
                    default: 
                        break;
                }
            }
        } 
        retrans--;
    }

    if (ActualState != ENDF) return -1;
    sendSFrame(fd, A_TR, C_UA);
    showStatistics = TRUE;
    return close(fd);


}


//S Frames Function
int sendSFrame(int fd, unsigned char A, unsigned char C){
    printf("Sending S Frame with C = 0x%02X\n", C);
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
        printf("Tx Byte read: 0x%02X\n", byte);

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
    while (!alarmEnabled && ActualState != ENDF) { 
        int bytes_readUA = read(fd, &byte, 1);  // Reading UA
        printf("Rx Byte read: 0x%02X\n", byte);  
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
    printf("over\n");
    return finishedC;
}



int sendIFrame(int fd, const unsigned char *data, int dataSize){
    unsigned char Iframe[MAX_IFRAME_SIZE];

    int Index = 0;
    
    // Header
    Iframe[Index++] = FLAG;
    Iframe[Index++] = A_TR;
    printf("NsTx in IFrame: 0x%02X\n", next_NsTx);
    Iframe[Index++] = next_NsTx; // C
    Iframe[Index++] = A_TR ^ next_NsTx; // BCC1

    // Calculate BCC2 before stuffing
    unsigned char BCC2 = data[0];
    for (int i = 1; i < dataSize; i++) {
        BCC2 ^= data[i];
    }

    // Stuff data bytes
    for (int i = 0; i < dataSize; i++) {
        if (data[i] == FLAG) {
            Iframe[Index++] = ESC;
            Iframe[Index++] = FLAG ^ 0x20;
        } else if (data[i] == ESC) {
            Iframe[Index++] = ESC;
            Iframe[Index++] = ESC ^ 0x20;
        } else {
            Iframe[Index++] = data[i];
        }

        if (Index >= MAX_IFRAME_SIZE - 3) {
            printf("ERROR: Frame too large\n");
            return -1;
        }
    }

    // Stuff BCC2 if needed
    if (BCC2 == FLAG || BCC2 == ESC) {
        Iframe[Index++] = ESC;
        Iframe[Index++] = BCC2 ^ 0x20;
    } else {
        Iframe[Index++] = BCC2;
    }

    // Trailer
    Iframe[Index++] = FLAG;

    // Send frame
    if (write(fd, Iframe, Index) < 0) {
        printf("ERROR: Error writing Iframe\n");
        return -1;
    }

    return Index;
}