/* server32.c — ОДНА программа, ДВА режима, запускаются одновременно:
 *   ./server32 async  — асинхронный I/O: SIGPOLL (ioctl I_SETSIG) + sigsuspend().
 *                       Все клиенты обслуживаются параллельно. /tmp/upper32_a.sock
 *   ./server32 sync   — синхронный I/O: блокирующие accept()/read(), клиенты
 *                       обслуживаются ПОСЛЕДОВАТЕЛЬНО (по одному). /tmp/upper32_s.sock
 * Ключ bench в любом режиме — считать, но не печатать (для замера скорости).
 * gcc -Wall -Wextra -std=gnu11 -D__EXTENSIONS__ -o server32 server32.c -lsocket -lnsl */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <sys/stream.h>     /* S_INPUT, S_MSG, S_HANGUP, S_ERROR */
#include <sys/stropts.h>    /* I_SETSIG  */
#include <sys/filio.h>      /* FIOASYNC  */

#ifndef SIGIO
#define SIGIO SIGPOLL       /* в Solaris это один и тот же сигнал (22) */
#endif

#define P_ASYNC "/tmp/upper32_a.sock"
#define P_SYNC  "/tmp/upper32_s.sock"
#define MAXC    64
#define LINE    512
#define MAXRD   32          /* максимум read() с одного fd за проход */

static const char *tag;                 /* "ASYNC" | "SYNC " */
static int mode_async, quiet, srvfd;
static int fd[MAXC];
static char lb[MAXC][LINE];
static size_t ln[MAXC];
static struct timespec tprev[MAXC];
static int tprev_ok[MAXC];
static sigset_t sleep_mask;
static struct timespec t0, tf, tl;
static long n_conn, n_msg, n_byte, n_wake;
static volatile sig_atomic_t pend, stop;

static void on_io(int s)  { (void)s; pend = 1; }
static void on_end(int s) { (void)s; stop = 1; }

static double el(struct timespec *a, struct timespec *b)
{
    return (b->tv_sec - a->tv_sec) + (b->tv_nsec - a->tv_nsec) / 1e9;
}

/* СТРОКА ЦЕЛИКОМ одним write(): два сервера пишут в один терминал, и только
   одиночный вызов гарантирует, что чужая строка не вклеится в середину нашей */
static void say(const char *fmt, ...)
{
    static char buf[LINE + 160];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof buf - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    buf[n++] = '\n';
    if (write(STDOUT_FILENO, buf, (size_t)n) < 0) { /* tty закрыт — игнорируем */ }
}

static void stamp(char *dst, size_t n)
{
    struct timespec t;
    long s, ns;
    clock_gettime(CLOCK_MONOTONIC, &t);
    s = (long)(t.tv_sec - t0.tv_sec);
    ns = (long)(t.tv_nsec - t0.tv_nsec);
    if (ns < 0) { ns += 1000000000L; s--; }
    snprintf(dst, n, "t=%5ld.%03ld", s, ns / 1000000L);
}

/* одна законченная строка клиента -> в ВЕРХНЕМ РЕГИСТРЕ, с измеренным gap */
static void emit(int slot, int c, const char *s, size_t n)
{
    char up[LINE], ts[32];
    struct timespec g;
    long gap = -1;
    size_t i;

    clock_gettime(CLOCK_MONOTONIC, &g);
    if (tprev_ok[slot]) gap = (long)(el(&tprev[slot], &g) * 1000.0);
    tprev[slot] = g; tprev_ok[slot] = 1;

    clock_gettime(CLOCK_MONOTONIC, &tl);
    if (n_msg == 0) tf = tl;
    n_msg++;
    if (quiet) return;

    if (n > sizeof up - 1) n = sizeof up - 1;
    for (i = 0; i < n; i++) up[i] = (char)toupper((unsigned char)s[i]);
    up[n] = 0;
    stamp(ts, sizeof ts);
    if (gap >= 0) say("%s  %s fd=%-3d gap=%5ldms | %s", ts, tag, c, gap, up);
    else          say("%s  %s fd=%-3d              | %s", ts, tag, c, up);
}

static void feed(int slot, int c, const char *b, ssize_t n)
{
    ssize_t i;
    for (i = 0; i < n; i++)
        if (b[i] == '\n') { if (ln[slot]) emit(slot, c, lb[slot], ln[slot]); ln[slot] = 0; }
        else if (ln[slot] + 1 < LINE) lb[slot][ln[slot]++] = b[i];
        else ln[slot] = 0;
}

static void arm_sig(int f)
{
    int on = 1, fl = fcntl(f, F_GETFL, 0);
    fcntl(f, F_SETOWN, getpid());
    fcntl(f, F_SETFL, (fl < 0 ? 0 : fl) | O_NONBLOCK);
    (void) ioctl(f, FIOASYNC, &on);
    if (ioctl(f, I_SETSIG, S_INPUT | S_MSG | S_HANGUP | S_ERROR) < 0) perror("I_SETSIG");
}

static int bind_listen(const char *path)
{
    struct sockaddr_un a;
    unlink(path);
    srvfd = socket(AF_UNIX, SOCK_STREAM, 0);
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    strncpy(a.sun_path, path, sizeof a.sun_path - 1);
    if (srvfd < 0 || bind(srvfd, (void *)&a, sizeof a) || listen(srvfd, 64) < 0) {
        perror("socket/bind/listen"); exit(1);
    }
    return srvfd;
}

static void summary(void)
{
    double e = el(&tf, &tl);
    say("===== ИТОГ server32 [%s] =====", tag);
    if (mode_async)
        say("клиентов=%ld  сообщений=%ld  байт=%ld  пробуждений ПО СИГНАЛУ=%ld",
            n_conn, n_msg, n_byte, n_wake);
    else
        say("клиентов=%ld  сообщений=%ld  байт=%ld  (каждый обслужен ПОСЛЕДОВАТЕЛЬНО)",
            n_conn, n_msg, n_byte);
    say("интервал приёма данных (1-е -> последнее сообщение) = %.3f с", e);
    if (e > 0 && n_msg)
        say("СКОРОСТЬ = %.0f сообщ/с, %.0f КБ/с", n_msg / e, n_byte / e / 1024.0);
    {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        say("время жизни сервера = %.3f с", el(&t0, &now));
    }
    say("#SUMMARY %s %.3f %ld %ld %.0f %.0f", tag, e, n_msg, n_byte,
        e > 0 ? n_msg / e : 0, e > 0 ? n_byte / e / 1024.0 : 0);
}

/* ---------- АСИНХРОННЫЙ РЕЖИМ: ни poll, ни select, только SIGPOLL ---------- */
static void run_async(void)
{
    char buf[8192];
    int i, c, busy;
    ssize_t n;

    arm_sig(srvfd);
    while (!stop) {
        while (!pend && !stop)
            sigsuspend(&sleep_mask);       /* сон в ядре; будит SIGPOLL */
        if (stop) break;
        pend = 0; n_wake++; busy = 0;

        while ((c = accept(srvfd, NULL, NULL)) >= 0) {   /* до EAGAIN */
            int slot = -1;
            for (i = 0; i < MAXC; i++) if (fd[i] == 0) { slot = i; break; }
            if (slot < 0) { close(c); continue; }
            fd[slot] = c; arm_sig(c); n_conn++;
            ln[slot] = 0; tprev_ok[slot] = 0;
            if (!quiet) { char ts[32]; stamp(ts, sizeof ts);
                          say("%s  %s + клиент fd=%d (слот %d)", ts, tag, c, slot); }
        }

        for (i = 0; i < MAXC; i++) {
            int reps = 0;
            if (fd[i] <= 0) continue;
            for (;;) {
                n = read(fd[i], buf, sizeof buf);
                if (n > 0) {
                    n_byte += n; feed(i, fd[i], buf, n);
                    if (++reps >= MAXRD) { busy = 1; break; }
                    continue;
                }
                if (n < 0 && errno == EINTR) continue;
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
                if (n == 0) {
                    int left = 0;
                    close(fd[i]); fd[i] = 0; ln[i] = 0; tprev_ok[i] = 0;
                    for (c = 0; c < MAXC; c++) if (fd[c] > 0) left++;
                    if (!quiet) { char ts[32]; stamp(ts, sizeof ts);
                                  say("%s  %s fd closed, осталось %d", ts, tag, left); }
                } else { close(fd[i]); fd[i] = 0; ln[i] = 0; tprev_ok[i] = 0; }
                break;
            }
        }
        if (busy) pend = 1;                /* очередь не пуста — не спать */
    }
}

/* ---------- СИНХРОННЫЙ РЕЖИМ: блокирующие вызовы, клиенты по одному -------- */
static void run_sync(void)
{
    static char buf[8192];

    while (!stop) {
        int c = accept(srvfd, NULL, NULL);   /* БЛОКИРУЕТСЯ: следующий не принят */
        if (c < 0) { if (errno == EINTR && stop) break; continue; }
        n_conn++;
        ln[0] = 0; tprev_ok[0] = 0;
        if (!quiet) { char ts[32]; stamp(ts, sizeof ts);
                      say("%s  %s + клиент fd=%d принят (остальных не слушаю)", ts, tag, c); }

        for (;;) {                           /* обслуживаем ОДНОГО до EOF */
            ssize_t n = read(c, buf, sizeof buf);   /* БЛОКИРУЕТСЯ до данных */
            if (n > 0) { n_byte += n; feed(0, c, buf, n); continue; }
            if (n < 0 && errno == EINTR && !stop) continue;
            if (n == 0 || (n < 0 && (errno == EINTR || errno == EAGAIN))) {
                close(c);
                if (!quiet) { char ts[32]; stamp(ts, sizeof ts);
                              say("%s  %s fd=%d закончил", ts, tag, c); }
            } else if (n < 0) close(c);
            break;
        }
        if (stop) break;
    }
}

int main(int argc, char **argv)
{
    struct sigaction sa;
    sigset_t blk;
    int i;
    const char *path = P_ASYNC;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "bench")) quiet = 1;
        else if (!strcmp(argv[i], "sync")) { mode_async = 0; path = P_SYNC; }
        else if (!strcmp(argv[i], "async")) { mode_async = 1; path = P_ASYNC; }
        else { fprintf(stderr, "usage: %s [async|sync] [bench]\n", argv[0]); return 1; }
    }
    tag = mode_async ? "ASYNC" : "SYNC ";

    memset(&sa, 0, sizeof sa);
    sa.sa_flags = 0;                       /* БЕЗ SA_RESTART — сон/чтение прервутся */
    sa.sa_handler = on_end;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sa.sa_handler = on_io;
    sigaction(SIGIO, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    bind_listen(path);

    if (mode_async) {
        /* ГОНКИ НЕТ: SIGIO заморожен, sigsuspend() атомарно снимает блокировку
           и засыпает — сигнал между проверкой флага и сном не потеряется. */
        sigemptyset(&blk); sigaddset(&blk, SIGIO);
        sigemptyset(&sleep_mask);
        sigprocmask(SIG_BLOCK, &blk, &sleep_mask);
    }

    clock_gettime(CLOCK_MONOTONIC, &t0);
    say("Server 32 [%s] на %s pid=%d  (%s)", tag, path, (int)getpid(),
        mode_async ? "SIGPOLL + sigsuspend, без poll/select"
                   : "блокирующие accept()/read(), клиенты по одному");

    if (mode_async) run_async(); else run_sync();

    say("");
    summary();
    for (i = 0; i < MAXC; i++) if (fd[i] > 0) close(fd[i]);
    close(srvfd); unlink(path);
    return 0;
}
