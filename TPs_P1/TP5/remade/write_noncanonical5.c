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
#define C_REJ0 = 0x01;
#define C_REJ1 = 0x81;

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
//S Frames Functions
int sendSFrame(int fd, unsigned char A, unsigned char C){

    unsigned char buf_sf[5] = {FLAG, A, C, C ^ A, FLAG};

    return write(fd, buf_sf, 5);
}

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
    while (!alarmEnabled && ua_received == 0) { //ua_received pd ser o llopen case llTx
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
    // Create string to send
    unsigned char IFrame_buf[BUF_SIZE] = {0x7E, 0x03, 0x03, 0x02, (0x03 ^ 0x01), 0x8D, 0x7F, 0x6D, 0x7E};


    // In non-canonical mode, '\n' does not end the writing.
    // Test this condition by placing a '\n' in the middle of the buffer.
    // The whole buffer must be sent even with the '\n'.
    //buf[5] = '\n';



    
  


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
