/**      (C)2000-2021 FEUP
 *       tidy up some includes and parameters
 * */

#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h> // for gethostbyname

#include <string.h>

#define BUFFER_SIZE 1024

//function to parse the FTP URL, to extract the host and port from an FTP URL
//example URL: ftp://user:password@host/path
int parse_ftp_url(const char *url, char *user, char *password, char *host, char *path, int *port) {
    if (!url || !user || !password || !host || !path || !port) {
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

    //user:password
    if (arroba) {
        *arroba = '\0';
        char *auth = auth_host_path;
        auth_host_path = arroba + 1;

        char *doisp = strchr(auth, ':');
        if (doisp) {
            *doisp = '\0';
            if (strlen(auth) >= BUFFER_SIZE) {
                fprintf(stderr, "User too long: exceeds %d characters\n", BUFFER_SIZE);
                return -1;
            }
            strncpy(user, auth, BUFFER_SIZE - 1);
            user[BUFFER_SIZE - 1] = '\0';

            if (strlen(doisp + 1) >= BUFFER_SIZE) {
                fprintf(stderr, "Password too long: exceeds %d characters\n", BUFFER_SIZE);
                return -1;
            }
            strncpy(password, doisp + 1, BUFFER_SIZE - 1);
            password[BUFFER_SIZE - 1] = '\0';
        } else {
            if (strlen(auth) >= BUFFER_SIZE) {
                fprintf(stderr, "User too long: exceeds %d characters\n", BUFFER_SIZE);
                return -1;
            }
            strncpy(user, auth, BUFFER_SIZE - 1);
            user[BUFFER_SIZE - 1] = '\0';
            password[0] = '\0'; // Empty password
        }
    } else {
        strncpy(user, "anonymous", BUFFER_SIZE - 1);
        user[BUFFER_SIZE - 1] = '\0';
        strncpy(password, "anonymous@", BUFFER_SIZE - 1);
        password[BUFFER_SIZE - 1] = '\0';
    }

    // Handle host and port
    char *barra = strchr(auth_host_path, '/');
    char *doisp = strchr(auth_host_path, ':');
    if (!barra) {
        fprintf(stderr, "Invalid URL: must contain a path\n");
        return -1;
    }

    if (doisp && (doisp < barra || barra == NULL)) {
        *doisp = '\0';
        if (strlen(auth_host_path) >= BUFFER_SIZE) {
            fprintf(stderr, "Host too long: exceeds %d characters\n", BUFFER_SIZE);
            return -1;
        }
        strncpy(host, auth_host_path, BUFFER_SIZE - 1);
        host[BUFFER_SIZE - 1] = '\0';

        char *endptr;
        long port_num = strtol(doisp + 1, &endptr, 10);
        if (endptr == doisp + 1 || *endptr != '/' && *endptr != '\0') {
            fprintf(stderr, "Invalid port: must be numeric\n");
            return -1;
        }
        if (port_num < 1 || port_num > 65535) {
            fprintf(stderr, "Invalid port: must be between 1 and 65535\n");
            return -1;
        }
        *port = (int)port_num;
    } else {
        if (barra == auth_host_path) {
            fprintf(stderr, "Invalid URL: empty host\n");
            return -1;
        }
        *barra = '\0';
        if (strlen(auth_host_path) >= BUFFER_SIZE) {
            fprintf(stderr, "Host too long: exceeds %d characters\n", BUFFER_SIZE);
            return -1;
        }
        strncpy(host, auth_host_path, BUFFER_SIZE - 1);
        host[BUFFER_SIZE - 1] = '\0';
        *port = 21; // Default FTP port
    }

    // Handle path
    if (strlen(barra + 1) >= BUFFER_SIZE) {
        fprintf(stderr, "Path too long: exceeds %d characters\n", BUFFER_SIZE);
        return -1;
    }
    strncpy(path, barra + 1, BUFFER_SIZE - 1);
    path[BUFFER_SIZE - 1] = '\0';
    if (path[0] == '\0') {
        fprintf(stderr, "Invalid URL: empty path\n");
        return -1;
    }

    return 0;
}


int main(int argc, char **argv) {

    if (argc > 1)
        printf("**** No arguments needed. They will be ignored. Carrying ON.\n");
    int sockfd;
    struct sockaddr_in server_addr;
    char buf[] = "Mensagem de teste na travessia da pilha TCP/IP\n";
    size_t bytes;

    /*server address handling*/
    bzero((char *) &server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(SERVER_ADDR);    /*32 bit Internet address network byte ordered*/
    server_addr.sin_port = htons(SERVER_PORT);        /*server TCP port must be network byte ordered */

    /*open a TCP socket*/
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket()");
        exit(-1);
    }
    /*connect to the server*/
    if (connect(sockfd,
                (struct sockaddr *) &server_addr,
                sizeof(server_addr)) < 0) {
        perror("connect()");
        exit(-1);
    }
    /*send a string to the server*/
    bytes = write(sockfd, buf, strlen(buf));
    if (bytes > 0)
        printf("Bytes escritos %ld\n", bytes);
    else {
        perror("write()");
        exit(-1);
    }

    if (close(sockfd)<0) {
        perror("close()");
        exit(-1);
    }
    return 0;
}


