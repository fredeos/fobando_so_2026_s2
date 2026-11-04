#include <stdio.h>

#include "client/header.h"

int main(int argc, char *argv[]){
    /* Add your code here */

    printf("Hello from Client!\n");
    
    printf("Client version: %s\n", CLIENT_VERSION);

    printf("argc: %d\n", argc);

    return 0;
}