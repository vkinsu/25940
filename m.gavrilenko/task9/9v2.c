#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

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
        /*
         * Ребёнок.
         * Заменяем образ процесса на cat(1).
         * Если execlp вернулся — значит, произошла ошибка.
         */
        execlp("cat", "cat", argv[1], (char *)NULL);

        /* Сюда попадаем только при ошибке exec */
        perror("execlp");
        _exit(127);   /* 127 — традиционный код «команда не найдена» */
    }

    /*
     * Родитель.
     * Печатаем текст до ожидания.
     */
    printf("Родитель: запустил cat для файла %s, pid ребёнка = %d\n",
           argv[1], pid);
    fflush(stdout);

    /*
     * Ждём завершения именно этого ребёнка.
     * waitpid блокирует родителя, пока cat не закончит работу.
     */
    int status;
    pid_t w = waitpid(pid, &status, 0);
    if (w == -1) {
        perror("waitpid");
        return 1;
    }

    /*
     * Разбираем статус завершения.
     */
    if (WIFEXITED(status)) {
        printf("Родитель: cat завершился с кодом %d\n",
               WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        printf("Родитель: cat убит сигналом %d\n",
               WTERMSIG(status));
    } else {
        printf("Родитель: cat завершился неизвестным образом\n");
    }

    /*
     * Эта строка гарантированно выводится после всего вывода cat,
     * потому что waitpid уже дождался завершения ребёнка.
     */
    printf("Родитель: последняя строка (после wait)\n");

    return 0;
}