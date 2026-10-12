#define _DEFAULT_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s /dev/ttyUSB0\n", argv[0]);
        return 1;
    }

    int fd = open(argv[1], O_RDWR | O_NOCTTY);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        perror("tcgetattr");
        close(fd);
        return 1;
    }

    cfmakeraw(&tty);
    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);

    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;

    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 10;  // 1-second read timeout

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("tcsetattr");
        close(fd);
        return 1;
    }

    tcflush(fd, TCIOFLUSH);

    const char *message = "PING\n";

    if (write(fd, message, strlen(message)) < 0) {
        perror("write");
        close(fd);
        return 1;
    }

    printf("Sent: %s", message);

    char response[128];
    size_t used = 0;

    while (used < sizeof(response) - 1) {
        char ch;
        ssize_t n = read(fd, &ch, 1);

        if (n < 0) {
            if (errno == EINTR)
                continue;

            perror("read");
            close(fd);
            return 1;
        }

        if (n == 0) {
            fprintf(stderr, "Timeout waiting for response.\n");
            break;
        }

        if (ch == '\r')
            continue;

        if (ch == '\n') {
            response[used] = '\0';
            printf("Received: %s\n", response);
            break;
        }

        response[used++] = ch;
    }

    close(fd);
    return 0;
}
