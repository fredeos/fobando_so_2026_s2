#ifndef SERVER_HEADER_H
#define SERVER_HEADER_H

#define SERVER_VERSION "1.0.3"

/* mpirun -np 4 o mas: rank 0 TCP, resto workers persistentes resize/filtro.
 * PUERTO [--once] [--verify]. Un solo rank admite la prueba ACK antigua. */
int tcp_server_run(int argc, char **argv); // Inicia MPI y separa coordinador TCP de trabajadores.

#endif // SERVER_HEADER_H
