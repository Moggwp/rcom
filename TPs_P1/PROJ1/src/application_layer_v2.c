// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
// Criar a variável correspondente ao que é proveniente da DLL e atribuir os valores
    // dos diferentes atributos (role, serialPort, baudRate, nTries e timeout)
    LinkLayer dll;
    // Role (Tx ou Rx)
    if (strcmp(role, "tx") == 0){ // role: {"tx" ou"rx"}
        dll.role = LlTx;
    } else {
        dll.role = LlRx;
    }
    // Serial Port
    strcpy(dll.serialPort,serialPort);
    // Restantes
    dll.baudRate = baudRate;
    dll.nRetransmissions = nTries;
    dll.timeout = timeout;

    // Fazer a ligação com a DLL
    int fd = llopen(dll);
    if (fd < 0){
        perror("Erro de Ligação\n");
        return -1;
    }




switch (dll.role){

    case LlTx:{
        
        // Abrir o ficheiro para enviar
        FILE* fptr;
        fptr = fopen(filename, "rb");
        if (fptr == NULL){
            perror("O Ficheiro não foi encontrado\n");
            return -1;
        }

        fseek(file, 0L, SEEK_END);
        long int tamanho = ftell(file);  // tamanho do ficheiro
        fseek(file, 0L, SEEK_SET); // reposicionar o "cursor"
        
        //---------------------------------------------------ENVIO DE PACOTES DE CONTROLO--------------------------------------//

        // Construção dos pacotes de controlo a serem enviados para a DLL
        unsigned int control_size;
        unsigned char *controlPack = ControlPacket(2, filename, tamanho, &control_size); 
        
        // Envio do pacote de controlo para a DLL
        if(llwrite(fd, controlPack, control_size) == -1){
            printf("Erro no envio do pacote de controlo START");
            exit(-1);
        }

        //-------------------------------------------------ENVIO DE PACOTES DE DADOS-------------------------------------------//

        
        






        

        
        
        

        



        break;
        
    }

    case LlRx: {

        break;

    }
}

unsigned char * ControlPacket(const unsigned int c, const char* filename, long int length, unsigned int *size){

// ---------------------------------------------- PACOTES DE CONTROLO ---------------------------------------------//

// C = Control Field -> Valores : 2 para START e 3 para END
// T = 0 para o tamanho do ficheiro e =1 para o nome do ficheiro -> T1 é relativo ao tamanho do ficheiro e T2 é relatvo ao nome
// L = Tamanho de V (parameter value) em octects -> L1 : tamanho do ficheiro, L2: tamanho do nome do ficheiro
// V - parameter value -> V1 : conteúdo do ficheiro

// [C] [T1] [L1] [   V1   ] [T2] [L2] [    V2   ]

// size = 1 + 1 + 1 + L1 + 1 + 1 + L2

// ------------------------------------------------------------ // -----------------------------------------------// 

// Saber o numero de bits necessários para o tamanho do ficheiro
int bits = log2((float)length);
// Saber o numero de bytes necessários para o tamanho do ficheiro
int L1 = bits/8.0;

// Saber o tamanho do nome do ficheiro
int L2 = strlen(filename);

// Calcular o tamanho do pacote de controlo 
*size = 1 + 1 + 1 + L1 + 1 + 1 + L2;

// Construir o pacote de controlo
unsigned char *pacote = (unsigned char*)malloc(*size);


pacote[0] = c; // [C]
pacote[1] = 0; // [T1]
pacote[2] = L1; // [L1]

//  [V1]
for (unsigned char i = 0 ; i < L1 ; i++) {
    packet[2+L1-i] = length & 0xFF;
    length >>= 8;
}


pacote[2+L1] = 1; // [T2]
pacote[2+L1+1] = L2; // [L2]
strcpy(pacote[L2 + 1], filename); // [V2]
return pacote;



}

unsigned char *DataPacket(){


    
}



}



    
     
