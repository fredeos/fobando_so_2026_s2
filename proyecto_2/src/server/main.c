#include <stdio.h>

#include "server/header.h"

int main(int argc, char *argv[]){
    /* Add your code here */

    printf("Hello from Server!\n");

    printf("Server version: %s\n", SERVER_VERSION);

    printf("argc: %d\n", argc);

    return 0;
}