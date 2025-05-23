

// Write to serial port in non-canonical mode
//
// Modified by: Eduardo Nuno Almeida [enalmeida@fe.up.pt]

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h> //added


//link_layer.h flags
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

// Baudrate settings are defined in <asm/termbits.h>, which is
// included by <termios.h>
#define BAUDRATE B38400
#define _POSIX_SOURCE 1 // POSIX compliant source

#define FALSE 0
#define TRUE 1

#define BUF_SIZE 10 //num de bytes total

volatile int STOP = FALSE;
volatile int alarmEnabled = FALSE;
volatile int retry = FALSE; //added by me
int alarmCount = 0;
int timeout = 3;
int retrans = 3;
int ua_received = 0;
int ACK_received = 0;
unsigned char C_Byte;

//S Frames Functions
int sendSFrame(int fd, unsigned char A, unsigned char C){

    unsigned char buf_sf[5] = {FLAG, A, C, C ^ A, FLAG};

    return write(fd, buf_sf, 5);
}
unsigned char SendAndReadI_CFrame(int fd);

// user-defined function to handle alarms (handler function)
void alarmHandler(int signal)
{ //this function will be called when the alarm is triggered
    alarmEnabled = TRUE; // can be used to change a flag that increases the number of alarms
    alarmCount++;
    //retry = TRUE;
    printf("Alarm #%d\n", alarmCount); // no response, retransmitting SET
}

int main(int argc, char *argv[])
{
    // Program usage: Uses either COM1 or COM2
    const char *serialPortName = argv[1];

    if (argc < 2)
    {
        printf("Incorrect program usage\n"
               "Usage: %s <SerialPort>\n"
               "Example: %s /dev/ttyS1\n",
               argv[0],
               argv[0]);
        exit(1);
    }

    // Open serial port device for reading and writing, and not as controlling tty
    // because we don't want to get killed if linenoise sends CTRL-C.
    int fd = open(serialPortName, O_RDWR | O_NOCTTY);

    if (fd < 0)
    {
        perror(serialPortName);
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



typedef enum {
        START,
        FLAG_RCV, /*recebeu flag inicial(pd acontecer a qlqr altura)*/
        A_RCV,
        C_RCV,
        BCC1_OK,
        STOPF
    } State;

    State ActualState = START;


    unsigned char byte;

    // install the function signal to be automatically
    //invoked when the timer expires, invoking alarmHandler
    (void) signal(SIGALRM, alarmHandler);
    // Enviar o quadro SET



/////////////////////////////////////////
//SEND S frame SET
/////////////////////////////////////////
while(ua_received == 0 && alarmCount < retrans){

sendSFrame(fd, A_TR, C_SET);  // Envia SET
printf("SET frame sent\n");

alarm(timeout);  //alarmHandler is called after timeout
alarmEnabled = FALSE;

// waiting for Rx response
    while (!alarmEnabled) { //ua_received pd ser o llopen case llTx
        int bytes_readUA = read(fd, &byte, 1);  // Reading UA
        printf("Byte read: 0x%02X\n", byte);  // Depuração

        // State Machine for the byte (UA from Rx)
        if (bytes_readUA > 0) {
            switch (ActualState) {
                case START:
                    if (byte == FLAG) {
                        ActualState = FLAG_RCV;
                    }
                    break;

                case FLAG_RCV:
                    if (byte == A_RT) {
                        ActualState = A_RCV;
                    }
                    break;

                case A_RCV:
                    if (byte == C_UA) {
                        ActualState = C_RCV;
                    }
                    break;

                case C_RCV:
                    if (byte == (A_RT ^ C_UA)) {
                        ActualState = BCC1_OK;
                    }
                    break;

                case BCC1_OK:
                    if (byte == FLAG) {
                        ActualState = STOPF;
                        STOP = TRUE;
                        printf("UA frame received correctly.\n");
                        alarm(0);
                        SendAndReadI_CFrame(int fd); //send I frame and read RR or REJ
                        ua_received = 1;
                    }
                    break;

                default:
                    ActualState = START;
                    break;
            }
        }
    }

/*
// Quando o alarme expira, se não houver resposta
    if (STOP == FALSE && alarmCount == 3) {
        printf("Timeout occurred, retransmitting SET...\n");
        sendSFrame(fd, A_TR, C_SET);  // Retransmitir SET
        alarm(timeout);  // Reinicia o alarme para a próxima tentativa
    }
    */
}
    


    // In non-canonical mode, '\n' does not end the writing.
    // Test this condition by placing a '\n' in the middle of the buffer.
    // The whole buffer must be sent even with the '\n'.
    //buf[5] = '\n';
/////////////////////////////////////////
//SEND I frame SET
/////////////////////////////////////////
int sendIFrame(int fd, unsigned char *data, int dataSize){
    int IframeSize = dataSize + 6; /*dataSize + 6 bytes 
    header(4): FLAG, A, C, BCC1. trailer(2): BCC2, FLAG */

    //allocate memory for the I frame
    unsigned char *Iframe = malloc(IframeSize);
    if (!Iframe){
        printf("ERROR: malloc failed\n");
        return -1;
    }
    //Header:
    Iframe[0] = FLAG;
    Iframe[1] = A_TR;
    Iframe[2] = 0x00; //C, Ns
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
    //readCFrame(fd); //read RR or REJ

        // sends Iframe to serial port
        if (write(fd, Iframe, IframeSize) < 0) {
            free(Iframe);
            printf("ERROR: Error writing Iframe\n");
            return -1;
        }
    
        free(Iframe);
        return IframeSize;
    }
    /////////////////
    /////////////////


        /*if (data[i] == FLAG || data[i] == ESC){
            Iframe[DataIndex] = ESC;
            Iframe[DataIndex + 1] = data[i] ^ 0x20;SendAndReadI_CFrame(int fd)
        DataIndex++;
        */
    
    //////////////////////////
    //// read control frame
    //////////////////////////
    
    unsigned char SendAndReadI_CFrame(fd){
        // Create data string to send
        // app. layer gives data, link layer puts control...
        //unsigned char DataIFrame_buf[BUF_SIZE] = {FLAG, A_TR, C_NS0, A_TR ^ C_NS0, 0x41, FLAG, 0x42, ESC, 0x43, 0x7E};
        unsigned char DataIFrame_buf[3] = {FLAG, 0x00, 0x00}; //BCC2 == ESC tester
        int bytesI_sent = sendIFrame(fd, DataIFrame_buf, 3); //!! size on DLL.c ??
        // Send I Frame with byte stuffing
        if (bytesI_sent < 0){
            printf("Error sending Iframe\n");  
            return -1;
        }
        printf("Iframe sent\n"); //debug

        //waiting for RR or REJ flag from Rx
        ////////////
        alarm(timeout);  //alarmHandler is called after timeout
        alarmEnabled = FALSE;
        byte = 0;
        // waiting for Rx response
        while (!alarmEnabled && ACK_received == 0 && alarmCount < retrans) { //ua_received pd ser o llopen case llTx
            int bytes_RR_REJ = read(fd, &byte, 1); //read RR or REJ
            printf("Byte ACK read: 0x%02X\n", byte); //debug

            //State Machine for RR or REJ
            if (bytes_RR_REJ > 0){
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
                        if (byte == C_RR0 || byte == C_RR1 || byte == C_REJ0 || byte == C_REJ1){
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
                        if (byte == (A_RT ^ C_RR0) || byte == (A_RT ^ C_RR1) || byte == (A_RT ^ C_REJ0) || byte == (A_RT ^ C_REJ1)){
                            ActualState = BCC1_OK;
                            printf("BCC1 received\n");
                        }
                        else if (byte == FLAG){
                            ActualState = FLAG_RCV;
                        }
                        else {
                            ActualState = START;
                        }
                        break;

                    case BCC1_OK:
                        if (byte == FLAG){
                            ActualState = STOPF;
                            STOP = TRUE;
                            ACK_received = 1;
                            printf("RR or REJ frame received correctly.\n");
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


        
    

    //read
    /*int bytes_UArec = read(fd, UArec_buf, BUF_SIZE);
    printf("UA sent by the Receiver: ");
    for (int k = 0; k < BUF_SIZE; k++){
        printf("0x%02X ", UArec_buf[k]);
    }
    printf("\n"); */
    


    // Restore the old port settings
    if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    close(fd);

    return 0;
}
