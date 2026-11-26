#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <command> [args...]\n", argv[0]);
        return 1;
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        /*
         * Ребёнок.
         * argv[1] — имя команды, &argv[1] — массив аргументов,
         * где argv[1] станет argv[0] для новой программы.
         * execvp ищет команду в PATH.
         */
        execvp(argv[1], &argv[1]);

        /*
         * Сюда попадаем только при ошибке exec.
         * perror сам возьмёт errno и напечатает сообщение.
         * _exit, а не exit: не сбрасываем буферы stdio,
         * унаследованные от родителя при fork.
         */
        perror("execvp");
        _exit(127);   /* 127 — стандартный код «команда не найдена» */
    }

    /* Родитель: ждём именно этого ребёнка */
    int status;
    pid_t w = waitpid(pid, &status, 0);
    if (w == -1) {
        perror("waitpid");
        return 1;
    }

    /*
     * Разбираем status ТОЛЬКО через макросы.
     * Печатать status напрямую нельзя: там упакованы
     * код возврата (биты 8–15), номер сигнала (биты 0–6)
     * и флаг дампа ядра (бит 7).
     */
    if (WIFEXITED(status)) {
        printf("Команда завершилась с кодом: %d\n", WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        printf("Команда убита сигналом: %d\n", WTERMSIG(status));
    } else {
        printf("Команда завершилась неизвестным образом, status=%d\n", status);
    }

    return 0;
}
