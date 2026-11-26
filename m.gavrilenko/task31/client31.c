/* client31.c — поток текста с СЛУЧАЙНЫМИ паузами. В tty молчит (печатает сервер).
 * ./client31 ИМЯ [кол-во] [мин_мс] [макс_мс] [длина_строки]
 * gcc -Wall -Wextra -std=gnu11 -o client31 client31.c -lsocket -lnsl */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#define SOCK "/tmp/upper31.sock"

static int rnd(int lo, int hi)          /* случайное целое в [lo, hi] */
{
    if (hi <= lo) return lo;
    return lo + (int)(random() % (hi - lo + 1));
}

int main(int argc, char **argv)
{
    struct sockaddr_un a;
    char tail[1024], m[1200];
    unsigned seed;
    int s, i, d, cnt = 15, lo = 0, hi = 400, len = 30, k;

    if (argc < 2) {
        fprintf(stderr, "usage: %s NAME [count] [min_ms] [max_ms] [line_len]\n", argv[0]);
        return 1;
    }
    if (argc > 2) cnt = atoi(argv[2]);
    if (argc > 3) lo  = atoi(argv[3]);
    if (argc > 4) hi  = atoi(argv[4]);
    if (argc > 5) len = atoi(argv[5]);
    if (cnt < 1) cnt = 1;
    if (hi < lo) hi = lo;
    if (len < 1) len = 1;
    if (len > 1000) len = 1000;

    /* у каждого процесса своё семя: время + pid + адрес в стеке */
    seed = (unsigned)time(NULL) ^ ((unsigned)getpid() << 8) ^ (unsigned)(size_t)&seed;
    srandom(seed);

    /* текст в разнобой: строчные и ПРОПИСНЫЕ буквы, сервер переводит в ВЕРХНИЙ */
    for (k = 0; k < len; k++) {
        char c = (char)('a' + (k + (int)seed) % 26);
        tail[k] = (k % 2) ? (char)(c - 'a' + 'A') : c;
    }
    tail[len] = 0;

    s = socket(AF_UNIX, SOCK_STREAM, 0);
    a.sun_family = AF_UNIX;
    strcpy(a.sun_path, SOCK);
    if (s < 0 || connect(s, (void *)&a, sizeof a) < 0) { perror("connect"); return 1; }

    usleep((useconds_t)(rnd(0, 300) % 1000001) * 1000L);      /* случайный старт */

    for (i = 1; i <= cnt; i++) {
        snprintf(m, sizeof m, "%s#%05d %s\n", argv[1], i, tail);
        if (write(s, m, strlen(m)) < 0) break;
        d = rnd(lo, hi);                                       /* случайная пауза */
        if (d > 1000000) d = 1000000;
        if (i < cnt && d > 0) usleep((useconds_t)d * 1000L);
    }
    usleep(50000);
    close(s);
    return 0;
}
