#include "client/header.h"

int main(int argc, char *argv[])
{
    return tcp_client_run(argc, argv); // Delega argumentos y codigo de salida al modulo del cliente.
}
