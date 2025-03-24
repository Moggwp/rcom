// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename) {
    LinkLayer dll;
    printf("A\n");
    printf("A\n");
    // Role (Tx ou Rx)
    if (strcmp(role, "tx") == 0) {
        dll.role = LlTx;
    } else {
        dll.role = LlRx;
    }
    printf("B\n");
    // Serial Port
    strcpy(dll.serialPort, serialPort);
    // Restantes configurações
    dll.baudRate = baudRate;
    dll.nRetransmissions = nTries;
    dll.timeout = timeout;

    printf("Serial Port: %s\n", dll.serialPort);
    printf("Baud Rate: %d\n", dll.baudRate);
    printf("Retries: %d\n", dll.nRetransmissions);
    printf("Timeout: %d\n", dll.timeout);


    // Fazer a ligação com a DLL
    
    int fd = llopen(dll);
    printf("C");
    if (fd < 0) {
        perror("Erro de Ligação\n");
        return;
    }
    printf("open successfull");

    switch (dll.role) {
        case LlTx: {
            // Abrir o ficheiro para enviar
            FILE *fptr = fopen(filename, "rb");
            if (fptr == NULL) {
                perror("O Ficheiro não foi encontrado\n");
                return;
            }

            fseek(fptr, 0L, SEEK_END);
            long int tamanho = ftell(fptr);
            fseek(fptr, 0L, SEEK_SET);

            // Envio de pacotes de controlo
            unsigned int control_size;
            unsigned char *controlPackStart = ControlPacket(2, filename, tamanho, &control_size);
            
            if (llwrite(controlPackStart, control_size) == -1) {
                printf("Erro no envio do pacote de controlo START");
                exit(-1);
            }

            free(controlPackStart);

            

            unsigned char* conteudo_dados = getData(fd,tamanho);
            long int bytes = tamanho;
            int tamanho_dados;
            while(bytes>=0) {

                // MAX_PAYLOAD_SIZE é o tamanho máximo do pacote
                if (bytes > MAX_PAYLOAD_SIZE){
                    tamanho_dados = MAX_PAYLOAD_SIZE;
                }
                else{
                    tamanho_dados = bytes;
                }

                unsigned char* dados = (unsigned char*) malloc(tamanho_dados);
                memcpy(dados, conteudo_dados, tamanho_dados);
                int tamanho_pack;
                unsigned char * pack = DataPacket(dados, tamanho_dados, &tamanho_pack);

                if(llwrite(pack, tamanho_pack) == -1)
                {
                    printf("Erro nos pacotes de dados");
                    exit(-1);
                }
                bytes = bytes - MAX_PAYLOAD_SIZE;
                conteudo_dados = conteudo_dados + tamanho_dados;
                
                unsigned char *controlPackEnd = ControlPacket(3, filename, tamanho, &control_size);
                if (llwrite(controlPackEnd, control_size) == -1) {
                    printf("Erro no envio do pacote de controlo END");
                    exit(-1);
                }
            free(controlPackEnd);
            llclose(fd);
            break;

            }
        }

        case LlRx: {

            unsigned char *pacote = (unsigned char *) malloc(MAX_PAYLOAD_SIZE);
            int tamanho_pacote = -1; // garante que o valor inicial de tamanho é menor que 0, para ficar no loop
            while((tamanho_pacote = llread(pacote))<0); // só quando o valor do llread() for maior que 0 é que a leitura é bem sucedida. Por isso, até lá fica dentro do loop
            unsigned long int tamanho_rx = 0; // tamanho do pacote que vai ser recebido
            unsigned char* nome = Control(pacote, tamanho_pacote, &tamanho_rx); // aaa

            FILE* newFile = fopen((char *) nome, "wb+");
            if (!newFile) {
                perror("Erro ao criar arquivo\n");
                return;
            }
            while(1){
                while((tamanho_pacote = llread(pacote))<0);
                if(tamanho_pacote == 0) break;
                else if(pacote[0] |= 3){
                    unsigned char *buffer = (unsigned char*) malloc(tamanho_pacote);
                    Dados(pacote, tamanho_pacote, buffer);
                    fwrite(buffer, sizeof(unsigned char), tamanho_pacote -4, newFile);
                    free(buffer);
                }
                else continue;
            }
            
            fclose(newFile);
            break;

            default:
                exit(-1);
                break;
        }
    }
}

unsigned char *ControlPacket(const unsigned int c, const char *filename, long int length, unsigned int *size) {
    int bits = log2((float)length);
    int L1 = bits / 8.0;
    int L2 = strlen(filename);

    *size = 1 + 1 + 1 + L1 + 1 + 1 + L2;
    unsigned char *pacote = (unsigned char *)malloc(*size);
    
    if (!pacote) {
        perror("Erro ao alocar memória para o pacote de controle");
        return NULL;
    }

    pacote[0] = c;
    pacote[1] = 0;
    pacote[2] = L1;

    for (unsigned char i = 0; i < L1; i++) {
        pacote[2 + L1 - i] = length & 0xFF;
        length >>= 8;
    }

    pacote[2 + L1] = 1;
    pacote[2 + L1 + 1] = L2;
    memcpy(&pacote[2 + L1 + 2], filename, L2);

    return pacote;
}

unsigned char *DataPacket(unsigned char *data, int data_size, int *packet_size) {
    *packet_size = 1 + 1 + 1 + data_size;
    unsigned char *pacote = (unsigned char *)malloc(*packet_size);

    if (!pacote) {
        perror("Erro ao alocar memória para o pacote de dados");
        return NULL;
    }

    pacote[0] = 1;
    pacote[1] = (data_size >> 8) & 0xFF;
    pacote[2] = data_size & 0xFF;
    memcpy(pacote + 3, data, data_size);

    return pacote;
}

unsigned char *getData(FILE *fd, long int length) {
    unsigned char *content = (unsigned char *)malloc(sizeof(unsigned char) * length);
    
    if (!content) {
        perror("Erro ao alocar memória para os dados");
        return NULL;
    }

    size_t bytesRead = fread(content, 1, length, fd);
    if (bytesRead < length) {
        printf("Aviso: nem todos os bytes foram lidos do arquivo!\n");
    }

    return content; 
}

unsigned char* Control(unsigned char* pack, int tamanho, unsigned long int *filesize){

    if (pack[0] != 2) return NULL;

    int L1 = pack[2];
    memcpy(filesize, &pack[3], L1);

    int L2 = pack[3 + L1 + 1];
    unsigned char *filename = (unsigned char *)malloc(L2 + 1);
    memcpy(filename, &pack[3 + L1 + 2], L2);
    filename[L2] = '\0';

    return filename;

}

void Dados(unsigned char *pack, int tamanho, unsigned char *buffer) {
    int dataSize = (pack[1] << 8) | pack[2];
    memcpy(buffer, &pack[3], dataSize);
}