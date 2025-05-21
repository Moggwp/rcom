/**      (C)2000-2021 FEUP
 *       tidy up some includes and parameters
 * */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h> //for gethostbyname
#include <string.h>
#include <ctype.h> //for isdigit

#define BUFFER_SIZE 1024

/*
gcc clientTCP.c -o ftpTest
./ftpTest  ftp://user:password@host/path

PUBLIC Servers:
URL 1: ftp://ftp.up.pt/pub/gnu/emacs/elisp-manual-21-2.8.tar.gz
URL 2: ftp://demo:password@test.rebex.net/readme.txt
URL 3: ftp://anonymous:anonymous@ftp.bit.nl/speedtest/100mb.bin
*/

//function to parse the FTP URL, to extract the host and port from an FTP URL
//example URL: ftp://user:password@host/path
int split_ftp_url(const char *url, char *user, char *password, char *host, char *path) {
    if (!url || !user || !password || !host || !path) {
        fprintf(stderr, "Invalid input\n");
        return -1;
    }
    if (strncmp(url, "ftp://", 6) != 0) {
        fprintf(stderr, "Invalid URL: must start with ftp://\n");
        return -1;
    }
    char temp_url[BUFFER_SIZE];
    if (strlen(url) >= BUFFER_SIZE) {
        fprintf(stderr, "URL too long: exceeds max characters (%d)\n", BUFFER_SIZE);
        return -1;
    }
    //copy the URL to a temporary buffer to avoid modifying the original
    strncpy(temp_url, url, BUFFER_SIZE - 1);
    temp_url[BUFFER_SIZE - 1] = '\0'; //null termination

    // Skip ftp://
    char *auth_host_path = temp_url + 6; // Skip "ftp://"
    char *arroba = strchr(auth_host_path, '@'); // Find '@' to separate user:password from host/path
    //strchr returns a pointer to the first occurrence of the character in the string, strrchr to the last. NULL if not found.

    //user:password until @
    if (arroba) {
        *arroba = '\0'; //split the string into user:password and \0 host/path
        char *auth = auth_host_path; //auth points to the beginning of the string
        //auth_host_path points to the beginning of the host/path
        auth_host_path = arroba + 1;

        char *doisp = strchr(auth, ':'); //Find ':' to separate user from password
        if (doisp) {
            *doisp = '\0';
            if (strlen(auth) >= BUFFER_SIZE) {
                fprintf(stderr, "User too long: exceeds %d characters\n", BUFFER_SIZE);
                return -1;
            }
            strncpy(user, auth, BUFFER_SIZE - 1); //copy the user string to the user buffer
            user[BUFFER_SIZE - 1] = '\0';

            if (strlen(doisp + 1) >= BUFFER_SIZE) {
                fprintf(stderr, "Password too long: exceeds %d characters\n", BUFFER_SIZE);
                return -1;
            }
            strncpy(password, doisp + 1, BUFFER_SIZE - 1);
            password[BUFFER_SIZE - 1] = '\0';
        } else { //if ':' is not found, set password to empty string
            if (strlen(auth) >= BUFFER_SIZE) {
                fprintf(stderr, "User too long: exceeds %d characters\n", BUFFER_SIZE);
                return -1;
            }
            strncpy(user, auth, BUFFER_SIZE - 1);
            user[BUFFER_SIZE - 1] = '\0';
            password[0] = '\0'; // Empty password
        }
    } else { //if arroba is not found, set user and password to default values
        strncpy(user, "anonymous", BUFFER_SIZE - 1);
        user[BUFFER_SIZE - 1] = '\0';
        strncpy(password, "anonymous@", BUFFER_SIZE - 1);
        password[BUFFER_SIZE - 1] = '\0';
    }

    // Handle host and port
    char *barra = strchr(auth_host_path, '/');
    char *doisp = strchr(auth_host_path, ':');
    if (!barra) { //barra is not found
        fprintf(stderr, "Invalid URL: must contain a path\n");
        return -1;
    }

    if (doisp && (doisp < barra || barra == NULL)) { //doisp is found and before barra. host:port/path or host:port
        *doisp = '\0'; //split 
        if (strlen(auth_host_path) >= BUFFER_SIZE) {
            fprintf(stderr, "Host too long: exceeds %d characters\n", BUFFER_SIZE);
            return -1;
        }
        strncpy(host, auth_host_path, BUFFER_SIZE - 1); //copy the host string to the host buffer
        host[BUFFER_SIZE - 1] = '\0'; //null termination
        
        char *endptr; //Pointer to check for errors in strtol
        long port_num = strtol(doisp + 1, &endptr, 10); //convert the PORT string to a long integer, after the :
        if (endptr == doisp + 1 || *endptr != '/' && *endptr != '\0') { //check if the conversion was successful
            //port followed by /
            fprintf(stderr, "Invalid port: must be numeric\n");
            return -1;
        }
        if (port_num < 1 || port_num > 65535) { //valid range for TCP/UDP ports
            fprintf(stderr, "Invalid port: must be between 1 and 65535\n");
            return -1;
        }
        //*port = (int)port_num; //set the port to the converted value
    } else { //doisp is not found or barra is not found
        if (barra == auth_host_path) { //barra is at the beginning of the string, so there is no host
            fprintf(stderr, "Invalid URL: empty host\n");
            return -1;
        }
        *barra = '\0'; //null termination host
        if (strlen(auth_host_path) >= BUFFER_SIZE) {
            fprintf(stderr, "Host too long: exceeds %d characters\n", BUFFER_SIZE);
            return -1;
        }
        //no port was specified
        strncpy(host, auth_host_path, BUFFER_SIZE - 1); //copy the given host string to the host buffer
        host[BUFFER_SIZE - 1] = '\0';
        //*port = 21; //default FTP port
    }

    //path
    if (strlen(barra + 1) >= BUFFER_SIZE) {
        fprintf(stderr, "Path too long: exceeds %d characters\n", BUFFER_SIZE);
        return -1;
    }
    //barra + 1 points to the first character after /
    //copy the given path string to the path buffer
    strncpy(path, barra + 1, BUFFER_SIZE - 1);
    path[BUFFER_SIZE - 1] = '\0';
    if (path[0] == '\0') { //it requires a path for operations like RETR.
        fprintf(stderr, "Invalid URL: empty path\n");
        return -1;
    }

    return 0;
}

//extract file name from path
char *extract_filename(const char *path) {
    if (!path) { //i think we already check this in split_ftp_url
        fprintf(stderr, "Invalid path\n");
        return NULL;
    }
    const char *last_barra = strrchr(path, '/'); //find the last occurrence of /
    if (last_barra) { //if / is found
        //strdup duplicates the string and returns a pointer to the new string
        return strdup(last_barra + 1); //return a copy of the file name, last /path
    } else {
        return strdup(path); //if no / is found, return the whole path
    }
}

//connect to the server using its hostname and port
int server_connect(const char *host, int port) {
    int sockfd;
    struct sockaddr_in server_addr;

    //create a TCP socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket()");
        return -1;
    }

    //set up the server address structure
    bzero((char *)&server_addr, sizeof(server_addr)); //bzero erases the data: n bytes (sizeof(server_addr)) in the server_addr structure
    server_addr.sin_family = AF_INET; //set the address family to IPv4
    server_addr.sin_port = htons(port); //convert port to network byte order

    //convert host name to an IP address
    //hostent is a struct that contains information about the host
    struct hostent *he = gethostbyname(host);
    if (he == NULL) {
        herror("gethostbyname()");
        close(sockfd);
        return -1;
    }
    bcopy(he->h_addr_list[0], (char *)&server_addr.sin_addr.s_addr, he->h_length); //copy the IP address to the server address structure

    //connect to the server
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect()");
        close(sockfd);
        return -1; //connection failed
    }

    return sockfd; //return the socket file descriptor
}

//read response from the server
int read_resp(int sockfd, char *resp) {
    char buffer[BUFFER_SIZE];
    bzero(buffer, BUFFER_SIZE); //clear the buffer
    bzero(resp, BUFFER_SIZE); //clear the response buffer

    while (1) {
        int bytes = read(sockfd, buffer, sizeof(buffer) - 1);
        if (bytes < 0) {
            perror("Error reading");
            return -1;
        }
        if (bytes == 0) {
            fprintf(stderr, "Connection closed unexpectedly\n");
            return -1;
        }
        buffer[bytes] = '\0'; //Null-terminate the buffer
        if (strlen(resp) + strlen(buffer) >= BUFFER_SIZE - 1) {
            fprintf(stderr, "Response too long for buffer\n");
            return -1;
        }
        strncat(resp, buffer, BUFFER_SIZE - strlen(resp) - 1); //append, strncat appends non-null bytes and null-terminate

        //check for complete FTP response - line with 3-digit code + space
        char *line = strtok(resp, "\n");
        while (line != NULL) {
            if (strlen(line) >= 4 && line[3] == ' ') {
                return 0; //complete response found
            }
            line = strtok(NULL, "\n");
        }
    } //should not reach here if properly terminated
    fprintf(stderr, "Error: response not complete - missing termination line\n");
    return -1; 

    //example
    //Multi-Line Response: Server sends 220-Welcome to FTP server\n220 → loop continues until 220 , returns 0.
    //if the server sends a response with a 3-digit code followed by a space, it indicates the end of the response.
    //if 220 is not found, error
}

//send command to the server
int send_comm(int sockfd, const char *comm, char *resp) {
    char buffer[BUFFER_SIZE];
    bzero(buffer, BUFFER_SIZE); //clear the buffer

    snprintf(buffer, BUFFER_SIZE - 1, "%s\r\n", comm); //format the command, FTP commands end with \r\n
    //snprintf is safer than sprintf, it prevents buffer overflow by limiting the number of characters written to the buffer
    //BUFFER_SIZE - 1 is used to leave space for the null terminator
    buffer[BUFFER_SIZE - 1] = '\0'; //null-terminate the buffer

    if (write(sockfd, buffer, strlen(buffer)) < 0) { //send the command to the server
        perror("Error writing");
        return -1;
    }
    return read_resp(sockfd, resp); //read the response from the server
}

//function to parse the PASV response
#define BUFFER_SIZE 1024

int pasv_resp(const char *resp, char *ip, int *port) {
    if (!resp || !ip || !port) {
        fprintf(stderr, "Invalid input\n");
        return -1;
    }

    //check for 227 response code
    if (strstr(resp, "227") == NULL) {
        fprintf(stderr, "Error: PASV response does not contain 227\n");
        return -1;
    }

    //with ()
    const char *start = strstr(resp, "(");
    if (start) {
        start++; // Move past '('
    } else { //without ()
        //fallback: look for the first number (e.g., "192")
        start = resp;
        while (*start && !isdigit(*start)) start++;
        if (!*start) {
            fprintf(stderr, "Invalid PASV response: no address found\n");
            return -1;
        }
    }

    // Check for closing parenthesis (if parentheses used)
    const char *end = start;
    if (strstr(resp, "(")) {
        end = strchr(start, ')');
        if (!end) {
            fprintf(stderr, "Invalid PASV response: missing ')'\n");
            return -1;
        }
    } else { //without ()
        //find end of numbers space, , or .
        while (*end && (isdigit(*end) || *end == ',' || *end == '.')) end++;
    }

    if (end - start >= BUFFER_SIZE) {
        fprintf(stderr, "PASV address too long: exceeds %d characters\n", BUFFER_SIZE);
        return -1;
    }

    //parse IP and port
    int ip_octets[4], port_parts[2]; //ip is a 32 bit number in four octets
    int parsed = sscanf(start, "%d,%d,%d,%d,%d,%d", 
                        &ip_octets[0], &ip_octets[1], &ip_octets[2], &ip_octets[3], 
                        &port_parts[0], &port_parts[1]);
    if (parsed != 6) {
        fprintf(stderr, "Invalid PASV response: expected 6 numbers\n");
        return -1;
    }

    //validate IP octets
    for (int i = 0; i < 4; i++) {
        if (ip_octets[i] < 0 || ip_octets[i] > 255) {
            fprintf(stderr, "Invalid IP octet: %d\n", ip_octets[i]);
            return -1;
        }
    }

    //calculate and validate port
    long port_num = port_parts[0] * 256L + port_parts[1];
    if (port_num < 1 || port_num > 65535) {
        fprintf(stderr, "Invalid port: %ld\n", port_num);
        return -1;
    }

    //format IP address with points (e.g. 192.168.1.1) to the ip buffer
    snprintf(ip, BUFFER_SIZE - 1, "%d.%d.%d.%d", 
             ip_octets[0], ip_octets[1], ip_octets[2], ip_octets[3]);
    ip[BUFFER_SIZE - 1] = '\0'; //null termination
    *port = (int)port_num; //set the port to the calculated port number

    return 0;
}

//implmentation of the FTP Client
//argc = argument count, number of arguments - command line arguments including the program name
//argv = argument vector, array of arguments
/*./ftp_client ftp://user:pass@ftp.example.com/file.txt
argc = 2 (program name + one argument).
argv:

    argv[0] = "./ftp_client" (program name).
    argv[1] = "ftp://user:pass@ftp.example.com/file.txt" (URL argument).
    argv[2] = NULL (end of array).
*/
int main(int argc, char **argv) {

    if (argc != 2) {
        fprintf(stderr, "Error. RIght usage: %s ftp://user:password@host/path\n", argv[0]);
        exit(-1);
    }
    //parse the URL, argv[1]
    char user[BUFFER_SIZE], password[BUFFER_SIZE], host[BUFFER_SIZE], path[BUFFER_SIZE];
    if (split_ftp_url(argv[1], user, password, host, path) < 0) { //split the url
        fprintf(stderr, "Error parsing URL\n");
        exit(-1);
    }

    //use the parsed values for the FTP connection
    printf("Connecting to host: %s\n", host);
    int controller_sockfd = server_connect(host, 21); //connect to the server, port 21
    if (controller_sockfd < 0) {
        fprintf(stderr, "Error connecting to server\n");
        exit(-1);
    }

    //read the server response
    char resp[BUFFER_SIZE];
    if (read_resp(controller_sockfd, resp) < 0) { //read the response
        fprintf(stderr, "Error reading response\n");
        close(controller_sockfd);
        exit(-1);
    }
    printf("Server response: %s\n", resp);

    //commands
    char comm[BUFFER_SIZE];
    snprintf(comm, BUFFER_SIZE - 1, "USER %s", user); //format the USER command
    comm[BUFFER_SIZE - 1] = '\0'; //null termination
    
    //send the USER command
    if (send_comm(controller_sockfd, comm, resp) < 0) { //send the USER command
        fprintf(stderr, "Error sending USER command\n");
        close(controller_sockfd);
        exit(-1);
    }
    printf("Server response: %s\n", resp);


    snprintf(comm, BUFFER_SIZE - 1, "PASS %s", password); //format the PASS command
    comm[BUFFER_SIZE - 1] = '\0'; //null termination
    //send the PASS command
    if (send_comm(controller_sockfd, comm, resp) < 0) { //send the PASS command
        fprintf(stderr, "Error sending PASS command\n");
        close(controller_sockfd);
        exit(-1);
    }
    printf("Server response: %s\n", resp);

    snprintf(comm, BUFFER_SIZE - 1, "PASV %s", resp); //format the PASV command
    comm[BUFFER_SIZE - 1] = '\0'; //null termination
    //send the PASV command
    if (send_comm(controller_sockfd, "PASV", resp) < 0) { //send the PASV command
        fprintf(stderr, "Error sending PASV command\n");
        close(controller_sockfd);
        exit(-1);
    }
    printf("Server response: %s\n", resp);

    //parse the PASV response
    char ip[BUFFER_SIZE]; //ip / host address
    int port; //data port
    if (pasv_resp(resp, ip, &port) < 0) { //parse the PASV response
        fprintf(stderr, "Error parsing PASV response\n");
        close(controller_sockfd);
        exit(-1);
    }
    printf("PASV response: IP = %s, port = %d\n", ip, port);

    //connect to the data port
    int data_sockfd = server_connect(ip, port); //connect to the data port
    if (data_sockfd < 0) {
        fprintf(stderr, "Error connecting to data port\n");
        close(controller_sockfd);
        exit(-1);
    }

    //send the RETR command
    snprintf(comm, BUFFER_SIZE - 1, "RETR %s", path); //format the RETR command
    comm[BUFFER_SIZE - 1] = '\0'; //null termination

    if (send_comm(controller_sockfd, comm, resp) < 0) { //send the RETR command
        fprintf(stderr, "Error sending RETR command\n");
        close(data_sockfd);
        close(controller_sockfd);
        exit(-1);
    }
    printf("Server response: %s\n", resp);


    //read the file from the data port
    const char *filename = extract_filename(path); //extract the file name from the path
    if (!filename) {
        fprintf(stderr, "Error extracting file name\n");
        close(data_sockfd);
        close(controller_sockfd);
        exit(-1);
    }

    FILE *file = fopen(filename, "wb"); //open the file for writing
    if (!file) {
        fprintf(stderr, "Error opening file %s\n", filename);
        close(data_sockfd);
        close(controller_sockfd);
        exit(-1);
    }

    char buffer[BUFFER_SIZE];
    bzero(buffer, BUFFER_SIZE); //clear the buffer
    //read from the data port and write to the file
    while (1) {
        int bytes = read(data_sockfd, buffer, BUFFER_SIZE); //read from the data port
        if (bytes < 0) {
            perror("Error reading data");
            fclose(file);
            close(data_sockfd);
            close(controller_sockfd);
            exit(-1);
        }
        if (bytes == 0) { //end of file
            break;
        } //not error:

        //If fwrite succeeds, it writes exactly bytes bytes, so fwrite(buffer, 1, bytes, file) returns bytes.
        buffer[bytes] = '\0'; //null-terminate the buffer

        if (fwrite(buffer, 1, bytes, file) != bytes) {
        perror("Error writing to file");
        fclose(file);
        close(data_sockfd);
        close(controller_sockfd);
        exit(-1);
    }
        //fwrite(buffer, 1, bytes, file); //write to the file
    }
    fclose(file); //close the file
    close(data_sockfd); //close the data port

    if (read_resp(controller_sockfd, resp) < 0) { //read the response
        fprintf(stderr, "Error reading response\n");
        close(controller_sockfd);
        exit(-1);
    }
    printf("Server response: %s\n", resp);

    //send the QUIT command
    snprintf(comm, BUFFER_SIZE - 1, "QUIT"); //format the QUIT command
    comm[BUFFER_SIZE - 1] = '\0'; //null termination
    if (send_comm(controller_sockfd, comm, resp) < 0) { //send the QUIT command
        fprintf(stderr, "Error sending QUIT command\n");
        close(controller_sockfd);
        exit(-1);
    }
    printf("Server response: %s\n", resp);

    //close the control connection
    if (close(controller_sockfd) < 0) {
        perror("Error closing control connection");
        exit(-1);
    }

    return 0;
}

