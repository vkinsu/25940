#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file>\n", argv[0]);
        return 1;
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        /* Ребёнок: заменяем себя на cat */
        execlp("cat", "cat", argv[1], (char *)NULL);
        perror("execlp");   /* Сюда попадаем только при ошибке */
        _exit(127);
    }

    /* Родитель */
    printf("Родитель: запустил cat, pid ребёнка = %d\n", pid);
    fflush(stdout);
    printf("Родитель: последняя строка (без wait)\n");

    return 0;
}