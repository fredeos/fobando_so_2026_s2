#ifndef CLIENT_HEADER_H
#define CLIENT_HEADER_H

#define CLIENT_VERSION "1.0.0"

/* Ejecuta el envío TCP cifrado con los argumentos de main:
 * <IP_SERVIDOR> <PUERTO> <RUTA_IMAGEN>. Devuelve EXIT_SUCCESS/EXIT_FAILURE. */
int tcp_client_run(int argc, char **argv);

#endif // CLIENT_HEADER_H
