#ifndef CLIENT_HEADER_H
#define CLIENT_HEADER_H

#define CLIENT_VERSION "1.0.0"

/* Envia PNG/JPEG por TCP, solicita resize/filtro MPI y guarda un PNG autenticado.
 * IP PUERTO IMAGEN [--resize WxH|original] [--filter NOMBRE] [--output SALIDA.png].
 * --ack-only conserva la prueba antigua de recepcion. EXIT_SUCCESS/FAILURE. */
int tcp_client_run(int argc, char **argv); // Implementa el flujo de red invocado desde main().

#endif // CLIENT_HEADER_H
