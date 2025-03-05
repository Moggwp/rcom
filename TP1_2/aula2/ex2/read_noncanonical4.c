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

#define BUF_SIZE 5

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
    unsigned char byte;
    unsigned char ua_buf[BUF_SIZE] = {0x7E, 0x01, 0x07,(0x01 ^ 0x07), 0x7E};


    //state machine
    typedef enum {
        START,
        FLAG_RCV, /*recebeu flag inicial(pd acontecer a qlqr altura)*/
        A_RCV,
        C_RCV,
        BCC_OK,
        STOPSET
    } State;

    State ActualState = START;

    printf("\nWaiting for SET...\n"); //debug

    while (STOP == FALSE) //read is always on, until you shut it off
    {
        // Returns after 5 chars have been input

        //new read, just 1 byte!
        
        int bytes = read(fd, &byte, 1); //return: bytes == 1

        printf("Byte read: 0x%02X\n", byte); //debug

        /*for (int i = 0; i < bytes; i++){
            printf("0x%02X ", buf[i]); //print each byte in hexa
        }
*/
        //state
        if (bytes > 0){ //ja foi enviado algo pelo Tx
            switch (ActualState) {
                case START:
                    if(byte == 0x7E){//flag inicial
                        ActualState = FLAG_RCV;
                    }
                    break;
                
                case FLAG_RCV: //stuck in FLAG_RCV if byte == 0x7E
                    if (byte == 0x03){
                        ActualState = A_RCV;
                    }
                    else if (byte != 0x7E){ //Other_RCV
                        ActualState = START;
                    }
                    break;

                case A_RCV:
                    if (byte == 0x03){
                        ActualState = C_RCV;
                    }
                    else if (byte == 0x7E){//flag inicial, é pq é p comecar dnv
                        ActualState = FLAG_RCV; //back to FLAG_RCV
                    }
                    else{ //Other_RCV
                        ActualState = START;
                    }
                    break;
                
                case C_RCV:
                    if (byte == (0x03 ^ 0x03)) 
                        ActualState = BCC_OK;
                    else if (byte == 0x7E) 
                        ActualState = FLAG_RCV;
                    else
                        ActualState = START;
                    break;
                
                case BCC_OK: //last one
                    if (byte == 0x7E){
                        ActualState = STOPSET;
                        printf("SET frame received correctly, sending UA...\n");
                        //send UA debug
                        int bytes_ua = write(fd, ua_buf, BUF_SIZE);
                        printf("\nSent UA:");
                        for (int i = 0; i < BUF_SIZE; i++) {
                            printf("0x%02X  ", ua_buf[i]);
                        }
                        STOP = TRUE;
                    }
                    else{
                        ActualState = START; //error, not final flag
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
