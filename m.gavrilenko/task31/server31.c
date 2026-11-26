/* server31.c — Unix domain socket, МНОГО клиентов в 1 процессе через poll(2).
 * gcc -Wall -Wextra -std=gnu11 -o server31 server31.c -lsocket -lnsl */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>

#define SOCK "/tmp/upper31.sock"
#define MAXC 64
#define LINE 512

static int sfd;
static struct pollfd pf[1 + MAXC];
static char lb[MAXC][LINE];
static size_t ln[MAXC];
static struct timespec t0, tf, tl;
static long n_conn, n_msg, n_byte, n_wake;
static volatile sig_atomic_t stop;

static void on_end(int s) { (void)s; stop = 1; }

static void stamp(void)
{
    struct timespec t;
    long s, ns;
    clock_gettime(CLOCK_MONOTONIC, &t);
    s = (long)(t.tv_sec - t0.tv_sec);
    ns = (long)(t.tv_nsec - t0.tv_nsec);
    if (ns < 0) { ns += 1000000000L; s--; }
    printf("t=%5ld.%03ld ", s, ns / 1000000L);
}

/* строка клиента -> В ВЕРХНЕМ РЕГИСТРЕ, с меткой времени и fd */
static void emit(int slot, int fd, const char *s, size_t n)
{
    size_t i;
    (void)slot;
    clock_gettime(CLOCK_MONOTONIC, &tl);
    if (n_msg == 0) tf = tl;
    stamp();
    printf("fd=%-3d | ", fd);
    for (i = 0; i < n; i++) putchar(toupper((unsigned char) s[i]));
    putchar('\n');
    fflush(stdout);
    n_msg++;
}

static void feed(int slot, int fd, const char *b, ssize_t n)
{
    ssize_t i;
    for (i = 0; i < n; i++)
        if (b[i] == '\n') { if (ln[slot]) emit(slot, fd, lb[slot], ln[slot]); ln[slot] = 0; }
        else if (ln[slot] + 1 < LINE) lb[slot][ln[slot]++] = b[i];
        else ln[slot] = 0;
}

int main(void)
{
    struct sockaddr_un a;
    struct sigaction sa;
    char buf[8192];
    int i, c;
    ssize_t n;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_end;                 /* БЕЗ SA_RESTART: poll вернёт EINTR */
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    unlink(SOCK);
    sfd = socket(AF_UNIX, SOCK_STREAM, 0);
    a.sun_family = AF_UNIX;
    strcpy(a.sun_path, SOCK);
    if (sfd < 0 || bind(sfd, (void *)&a, sizeof a) || listen(sfd, 64) < 0) {
        perror("socket/bind/listen"); return 1;
    }
    { int fl = fcntl(sfd, F_GETFL, 0); fcntl(sfd, F_SETFL, fl | O_NONBLOCK); }

    for (i = 0; i < 1 + MAXC; i++) { pf[i].fd = -1; pf[i].events = 0; }
    pf[0].fd = sfd; pf[0].events = POLLIN;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    printf("Server 31 (poll(2), 1 процесс, без fork/потоков) %s pid=%d\n", SOCK, (int)getpid());
    fflush(stdout);

    while (!stop) {
        int r = poll(pf, 1 + MAXC, -1);        /* СПИМ В ЯДРЕ до любого события */
        if (r < 0) { if (errno == EINTR) continue; perror("poll"); break; }
        n_wake++;

        while ((c = accept(sfd, NULL, NULL)) >= 0) {      /* все pending-соединения */
            int slot = -1;
            for (i = 1; i < 1 + MAXC; i++) if (pf[i].fd == -1) { slot = i; break; }
            if (slot < 0) { close(c); continue; }
            { int fl = fcntl(c, F_GETFL, 0); fcntl(c, F_SETFL, fl | O_NONBLOCK); }
            pf[slot].fd = c; pf[slot].events = POLLIN; n_conn++;
        }

        for (i = 1; i < 1 + MAXC; i++) {
            if (pf[i].fd < 0 || !(pf[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            n = read(pf[i].fd, buf, sizeof buf);
            if (n > 0) { n_byte += n; feed(i - 1, pf[i].fd, buf, n); }
            else if (n == 0) {
                int left = 0;
                close(pf[i].fd); pf[i].fd = -1; pf[i].events = 0; ln[i - 1] = 0;
                for (c = 1; c < 1 + MAXC; c++) if (pf[c].fd > 0) left++;
                stamp(); printf("fd closed, осталось %d\n", left);
            } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                close(pf[i].fd); pf[i].fd = -1; pf[i].events = 0;
            }
        }
    }

    printf("\n===== ИТОГ (server31, poll(2)) =====\n");
    printf("клиентов=%ld  сообщений=%ld  байт=%ld  пробуждений poll=%ld\n",
           n_conn, n_msg, n_byte, n_wake);
    {
        double el = (tl.tv_sec - tf.tv_sec) + (tl.tv_nsec - tf.tv_nsec) / 1e9;
        if (el > 0 && n_msg)
            printf("время %0.3f с -> %0.0f сообщ/с, %0.0f КБ/с\n",
                   el, n_msg / el, n_byte / el / 1024.0);
    }
    for (i = 1; i < 1 + MAXC; i++) if (pf[i].fd > 0) close(pf[i].fd);
    close(sfd); unlink(SOCK);
    return 0;
}
