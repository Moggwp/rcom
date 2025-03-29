//actual prob: it writes after alarm#3 but says "ERROR" in the end
//even if UA was sent back. not sure if it's a problem

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


// user-defined function to handle alarms (handler function)
void alarmHandler(int signal)
{ //this function will be called when the alarm is triggered
    alarmEnabled = FALSE; // can be used to change a flag that increases the number of alarms
    alarmCount++;
    retry = TRUE;
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
        BCC_OK,
        STOPUA
    } State;

    State ActualState = START;




    // install the function signal to be automatically
    //invoked when the timer expires, invoking alarmHandler
    (void) signal(SIGALRM, alarmHandler);


    // Create string to send
    //random buff:
    //unsigned char set_buf[BUF_SIZE] = {0x7E, 0x03, 0x03, 0x02, (0x03 ^ 0x01), 0x8D, 0x7F, 0x6D, 0x7E};
    unsigned char set_buf[BUF_SIZE] = {0x7E, 0x03, 0x03, (0x03 ^ 0x03), 0x7E};


    // In non-canonical mode, '\n' does not end the writing.
    // Test this condition by placing a '\n' in the middle of the buffer.
    // The whole buffer must be sent even with the '\n'.
    //buf[5] = '\n';

    //sending data
    for (int i = 0; i < BUF_SIZE; i++){
        printf("Sending: 0x%02X\n", set_buf[i]);
    }
    //first write SET
    int bytes = write(fd, set_buf, BUF_SIZE); //return bytes num
    printf("%d bytes written (FIRST WRITE)\n", bytes);
    /*alarm(3); //timeout 3segs, if t>3segs, alarmEnabled == TRUE
    alarmEnabled = TRUE;*/


    // Wait until all bytes have been written to the serial port
    sleep(1);

    //buf to receive UA, answer from the Sender
    unsigned char byte;
    //retries max 3
    while (STOP == FALSE && alarmCount < 3){
        
        int bytesUA = read(fd, &byte, 1); //return: bytes == 1
        //printf("Byte read: 0x%02X\n", byte); //debug

        if (retry == TRUE){
            int bytes = write(fd, set_buf, BUF_SIZE); //return bytes num
            printf("%d bytes written\n", bytes);
            /*alarm(3); //timeout 3segs, if t>3segs, alarmEnabled == TRUE
            alarmEnabled = TRUE;
            retry = FALSE;*/
        }
    

        if (bytesUA > 0){ //ja foi enviado algo pelo Rx
            printf("Byte read: 0x%02X\n", byte); //debug
                switch (ActualState) {
                    case START:
                        if(byte == 0x7E){//flag inicial
                            ActualState = FLAG_RCV;
                        }
                        break;
                    
                    case FLAG_RCV: //stuck in FLAG_RCV if byte == 0x7E
                        if (byte == 0x01){
                            ActualState = A_RCV;
                        }
                        else if (byte != 0x7E){ //Other_RCV
                            ActualState = START;
                        }
                        break;

                    case A_RCV:
                        if (byte == 0x07){
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
                        if (byte == (0x01 ^ 0x07)) 
                            ActualState = BCC_OK;
                        else if (byte == 0x7E) 
                            ActualState = FLAG_RCV;
                        else
                            ActualState = START;
                        break;
                    
                    case BCC_OK: //last one
                        if (byte == 0x7E){
                            ActualState = STOPUA;
                            printf("UA frame received correctly.\n");
                            STOP = TRUE;
                            alarm(0);
                            }
                           
                        
                        else{
                            ActualState = START; //error, not final flag
                        }
                        break;

                    default:
                        ActualState = START; //not sure if it's needed

                }
        }
    }

    if (alarmCount >= 3){
        printf("ERROR: No UA received after 3 retries. \n");
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
