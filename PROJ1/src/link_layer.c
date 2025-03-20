// Link layer protocol implementation

#include "link_layer.h" // connectionParameters

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source

//var definition
int timeout = 0;
int retrans = 0;
int alarmEnabled = FALSE;
int alarmCount = 0;
/*timeout = connectionParameters.timeout;
    retrans = connectionParameters.nRetransmissions;*/



int connection (const char *serialPort){
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
}

// user-defined function to handle alarms (handler function)
void alarmHandler(int signal) { //this function will be called when the alarm is triggered
    alarmEnabled = TRUE; // can be used to change a flag that increases the number of alarms
    alarmCount++;
    //retry = TRUE;
    printf("Alarm #%d\n", alarmCount); // no response, retransmitting SET
}


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

            while (STOPF != TRUE && connectionParameters.nRetransmissions != 0){ //nRetransmissions left to send SET Frame (within timeout period ofc)
                sendSFrame(fd, A_TR, C_SET); //send SET
                alarm(connectionParameters.timeout);
                alarmEnabled = FALSE;
                //wait for UA
                while (ActualState != ENDF && alarmEnabled == FALSE) {
                    int bytes_read = read(fd, &byte, 1); //return: num bytes == 1
                    printf("Byte read: 0x%02X\n", byte); //debug

                    if (bytes_read > 0){

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
                            
                            case BCC1_OK: //last one
                                if (byte == 0x7E){
                                    ActualState = STOPF;
                                    printf("UA frame received correctly.\n");
             
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
                connectionParameters.nRetransmissions--; //decrement remaining retransmissions

            }
            if (ActualState != STOPF){
                return -1; //error, didn't receive UA
                }
                break;
           }


           case LlRx: {
            //receive SET, send UA
            while (ActualState != STOPF){
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
                    
                    case BCC1_OK: //last one
                        if (byte == FLAG){
                            printf("SET frame received correctly, sending UA...\n");
                            //send UA de0bug
                            sendSFrame(fd, A_RT, C_UA);
                            printf("Sent UA!\n");
                            ActualState = STOPF;
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
        default:
            return -1; //error if role is not LlTx or LlRx
            break;
    }
    return fd;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize)
{
    // TODO
    if (buf == NULL){
        return -1; //error, buf is NULL
    }

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
