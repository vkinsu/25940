#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/wait.h>

int main(void) {
    int fds[2];
    if (pipe(fds) == -1) {
        perror("pipe");
        return 1;
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        /* --- Ребёнок: пишет смешанный регистр в канал --- */
        close(fds[0]);   /* не читаем — закрываем */

        const char *msg =
            "sup guys!\n"
            "This is MiXeD CaSe Text 123.\n"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ\n"
            "abcdefghijklmnopqrstuvwxyz\n";

        ssize_t n = write(fds[1], msg, strlen(msg));
        if (n == -1) {
            perror("write");
            _exit(1);
        }

        close(fds[1]);   /* закрытие даёт EOF родителю */
        _exit(0);
    }

    /* --- Родитель: читает, переводит в верхний регистр, печатает --- */
    close(fds[1]);       /* не пишем — закрываем */

    char buf[4096];
    ssize_t n;
    while ((n = read(fds[0], buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            /* приведение к unsigned char обязательно:
               toupper с отрицательным char — UB */
            buf[i] = (char)toupper((unsigned char)buf[i]);
        }
        if (write(STDOUT_FILENO, buf, (size_t)n) == -1) {
            perror("write stdout");
            break;
        }
    }
    if (n == -1) perror("read");

    close(fds[0]);

    int status;
    if (waitpid(pid, &status, 0) == -1) perror("waitpid");

    return 0;
}
