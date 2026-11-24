#ifndef SERVER_HEADER_H
#define SERVER_HEADER_H

#define SERVER_VERSION "1.0.3"

/* Ejecuta el servidor TCP cifrado con <PUERTO> como argumento de main.
 * Atiende clientes secuencialmente hasta encontrar un error fatal. */
int tcp_server_run(int argc, char **argv);

#endif // SERVER_HEADER_H
