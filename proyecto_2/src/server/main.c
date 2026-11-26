#include "server/header.h"

int main(int argc, char *argv[])
{
    return tcp_server_run(argc, argv); // Delega el arranque TCP/MPI al modulo del servidor.
}
