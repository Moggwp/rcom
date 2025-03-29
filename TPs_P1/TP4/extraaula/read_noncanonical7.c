// Read from serial port in non-canonical mode
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

// Baudrate settings are defined in <asm/termbits.h>, which is
// included by <termios.h>
#define BAUDRATE B38400
#define _POSIX_SOURCE 1 // POSIX compliant source

#define FALSE 0
#define TRUE 1
#define BUF_SIZE 10

//link_layer.h flags
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
#define REJ0 = 0x01;
#define REJ1 = 0x81;

//S Frames Function
int sendSFrame(int fd, unsigned char A, unsigned char C){

    unsigned char buf_sf[5] = {FLAG, A, C, C ^ A, FLAG};

    return write(fd, buf_sf, 5);
}

volatile int STOP = FALSE; 

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

    // Open serial port device for reading and writing and not as controlling tty
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
    newtio.c_cc[VTIME] = 0; // Inter-character timer unused
    newtio.c_cc[VMIN] = 5;  // Blocking read until 5 chars received

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

    // Loop for input
    //buf written by Tx
    //unsigned char buf[BUF_SIZE + 1] = {0}; // +1: Save space for the final '\0' char
    //ua buf to send back
    unsigned char byte, bcc2, bcc2xor, data_buf[BUF_SIZE];
    int data_count = 0, set_received = 0;


    //state machine
    typedef enum {
        START,
        FLAG_RCV, /*recebeu flag inicial(pd acontecer a qlqr altura)*/
        A_RCV,
        C_RCV,
        BCC1,
        DATA,
        ENDF
    } State;

    State ActualState = START;

    printf("\nWaiting for SET...\n"); //debug

    while (STOP == FALSE) //read is always on, until you shut it off
    {
        // Returns after 5 chars have been input

        //new read, just 1 byte!
        
        int bytes_read = read(fd, &byte, 1); //return: num bytes == 1

        printf("Byte read: 0x%02X\n", byte); //debug

        /*for (int i = 0; i < bytes; i++){
            printf("0x%02X ", buf[i]); //print each byte in hexa
        }
*/      //SET Frame (1st Frame)
        if (bytes_read > 0 && set_received == 0){

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
                    if (byte == C_SET){
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
                    if (byte == (A_TR ^ C_SET)) 
                        ActualState = BCC1;
                    else if (byte == FLAG) 
                        ActualState = FLAG_RCV;
                    else
                        ActualState = START;
                    break;
                
                case BCC1: //last one
                    if (byte == FLAG){
                        printf("SET frame received correctly, sending UA...\n");
                        //send UA de0bug
                        sendSFrame(fd, A_RT, C_UA);
                        printf("Sent UA!\n");

                        ActualState = START; //start of the next state machine
                        set_received = 1;
                    }
                    else{
                        ActualState = START; //error, not final flag
                    }
                    break;

                default:
                    ActualState = START; //not sure if it's needed

            }

        }

        //I Frames
        //state 0
        if (bytes_read > 0 && set_received == 1){ //ja foi enviado algo pelo Tx
            printf("\nWaiting for I Frame\n");
            switch (ActualState) { //state 0
                case START:
                    if(byte == 0x7E){//flag inicial
                        ActualState = FLAG_RCV;
                        data_count = 0;
                        memset(data_buf, 0, BUF_SIZE); //empty data, data field is discarded
                        printf("State 0\n");
                    }
                    break;
                //state 1
                case FLAG_RCV: //stuck in FLAG_RCV if byte == 0x7E (F)
                    if (byte == 0x7E){
                        ActualState = FLAG_RCV;
                    }
                    else { //A_RCV
                          //isto e' redundante pq ja ta if bytes_read>0
                            ActualState = A_RCV; 
                            printf("1\n");
                        }
                        
                    
                    break;
                //state 2
                case A_RCV:
                 
                        ActualState = C_RCV;
                        printf("2\n");
                    //not sure se e' preciso um else para nao fzr logo break
                    break;
                
                    //state 3
                case C_RCV: //control: pode parar transmissao se == DISC
               
                        ActualState = BCC1;
                        printf("3\n");
                    break;
                //state 4
                case BCC1: //declarar A / C
                    printf("bcc1\n");
                    if (byte == (0x03 ^ 0x01)){ //bcc1 correct
                        ActualState = DATA;
                        printf("4\n"); //debug
    
                    }
                    else{ //incorrect bcc1
                        ActualState = START; //error, not final flag
                        printf("Incorrect bcc1\n");
                    }
                    break;
                
                //state 5
                case DATA:
                    if (byte != 0x7E){ 
                        ActualState = DATA; //mantêm-se a ler data
                        data_buf[data_count] = byte;
                        data_count++;
                        printf("data, %d\n", data_count);
                    }
                    else if (byte == 0x7E){ //final flag
                        bcc2 = data_buf[(data_count - 1)]; //bcc2 e' o ultimo byte antes da flag
                        bcc2xor = 0; //reset caso tenha ido usado antes
                        for (int k = 0; k < data_count; k++){
                            bcc2xor ^= data_buf[k];
                            printf("data byte: 0x%02X\n", data_buf[k]);
                            
                        }
                        printf("bcc2xor is 0x%02X\n", bcc2xor);
                            if(bcc2 == bcc2xor){ //correct data, send RR
                                ActualState = ENDF;
                                printf("STOP\n");
                                STOP = TRUE;
                            }
                            else{ //incorrect data, send REJ
                                ActualState = START;
                                printf("wrong BCC2\n");
                            }
                        
                    }   
                    break;

                default:
                    ActualState = START; //not sure if it's needed

            }

        }
  

        //printf(":%s:%d\n", buf, bytes);
        //printf("\n");

        /*if (byte == 'z'){
            STOP = TRUE;
        }*/
    }

    // The while() cycle should be changed in order to respect the specifications
    // of the protocol indicated in the Lab guide

    // Restore the old port settings
    if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    close(fd);

    return 0;
}
