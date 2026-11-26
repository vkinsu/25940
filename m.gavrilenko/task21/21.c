#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <termios.h>

static volatile sig_atomic_t beep_count = 0;
static volatile sig_atomic_t quit_flag  = 0;

static void on_sigint(int sig) {
    (void)sig;
    beep_count++;
    write(STDOUT_FILENO, "\a", 1);
}

static void on_sigquit(int sig) {
    (void)sig;
    quit_flag = 1;
}

int main(void) {
    struct termios orig, t;
    int use_termios = (tcgetattr(STDIN_FILENO, &orig) == 0);
    if (use_termios) {
        t = orig;
        t.c_lflag &= ~(ECHOCTL | ECHO);   /* ни ^C, ни букв */
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
    }

    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = on_sigint;
    sa.sa_flags   = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction SIGINT");
        if (use_termios) tcsetattr(STDIN_FILENO, TCSANOW, &orig);
        return 1;
    }

    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = on_sigquit;
    sa.sa_flags   = 0;
    if (sigaction(SIGQUIT, &sa, NULL) == -1) {
        perror("sigaction SIGQUIT");
        if (use_termios) tcsetattr(STDIN_FILENO, TCSANOW, &orig);
        return 1;
    }

    printf("Пищалка запущена. PID = %d\n", (int)getpid());
    printf("Ctrl-C (SIGINT)  — писк\n");
    printf("Ctrl-\\ (SIGQUIT) — выход со счётчиком\n");
    fflush(stdout);

    while (!quit_flag) {
        pause();
    }

    if (use_termios) tcsetattr(STDIN_FILENO, TCSANOW, &orig);

    printf("\nЗвуковой сигнал прозвучал %d раз(а).\n", (int)beep_count);
    return 0;
}
