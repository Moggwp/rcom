// Link layer protocol implementation

#include "link_layer.h" // connectionParameters

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source

//var definition
int timeout = 0;
int retrans = 0;
int alarmEnabled = FALSE;
/*timeout = connectionParameters.timeout;
    retrans = connectionParameters.nRetransmissions;*/
////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters) // param. given by app layer
{
    State ActualState = START;

    int fd = connection(connectionParameters.serialPort); //file descriptor of serialPort
    if (fd < 0){
        return -1; //error opening serial port
    }

    unsigned char byte;

    switch (connectionParameters.role){
        case LlTx: { //send SET to Rx

            (void) signal(SIGALRM, alarmHandler);

            while (STOP != TRUE && connectionParameters.nRetransmissions != 0){
                sendSFrame(fd, A_TR, C_SET);
                alarm(connectionParameters.timeout);
                alarmEnabled = FALSE;

                while (ActualState != END && alarmEnabled == FALSE) {

                }
           }

        }
    }
        

    return 1;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize)
{
    // TODO

    return 0;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    // TODO

    return 0;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics)
{
    // TODO

    return 1;
}
