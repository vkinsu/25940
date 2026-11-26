#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/un.h>

#define SOCK_PATH "/tmp/upper30.sock"

int main(void) {
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s == -1) { perror("socket"); return 1; }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path) - 1);

    unlink(SOCK_PATH);
    if (bind(s, (struct sockaddr *)&addr, sizeof addr) == -1) { perror("bind"); return 1; }
    if (listen(s, 1) == -1) { perror("listen"); return 1; }

    printf("Сервер слушает %s\n", SOCK_PATH);
    fflush(stdout);

    int c = accept(s, NULL, NULL);
    if (c == -1) { perror("accept"); return 1; }

    printf("Клиент подключён (fd=%d)\n", c);
    fflush(stdout);

    char buf[4096];
    ssize_t n;
    while ((n = read(c, buf, sizeof buf)) > 0) {
        for (ssize_t i = 0; i < n; i++)
            buf[i] = (char)toupper((unsigned char)buf[i]);
        if (write(STDOUT_FILENO, buf, (size_t)n) == -1) break;
    }

    close(c);
    close(s);
    unlink(SOCK_PATH);
    printf("\nСервер завершён\n");
    return 0;
}
