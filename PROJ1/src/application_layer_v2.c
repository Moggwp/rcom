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
        printf("Erro de Ligação\n");
        exit(-1);
    }



switch (dll.role){

    case LlTx:{
        
        // Abrir o ficheiro para enviar
        FILE* fptr;
        fptr = fopen(filename, "rb");
        if (fptr == NULL){
            printf("O Ficheiro não foi encontrado\n");
            exit(-1);
        }
        break;
        
    }

    case LlRx: {

        break;

    }
}
}
int main(){
    
}
