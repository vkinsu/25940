/* client32.c — случайная пауза, случайный текст, пауза напечатана в строке.
 * UPPER_SOCK=/tmp/upper32_s.sock ./client32 ИМЯ [кол-во] [мин_мс] [макс_мс] [длина] [seed]
 * gcc -Wall -Wextra -std=gnu11 -o client32 client32.c -lsocket -lnsl */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

static unsigned long seed;

static int rnd(int lo, int hi)
{
    unsigned long x;
    if (hi <= lo) return lo;
    seed = seed * 6364136223846793005UL + 1442695040888963407UL;
    x = (seed >> 33) & 0x7fffffffUL;
    return lo + (int)(x % (unsigned long)(hi - lo + 1));
}

int main(int argc, char **argv)
{
    struct sockaddr_un a;
    char txt[1024], m[1400], path[108];
    const char *name, *env;
    int s, i, d, cnt = 12, lo = 50, hi = 700, len = 22, k;

    if (argc < 2) {
        fprintf(stderr, "usage: %s NAME [count] [min_ms] [max_ms] [line_len] [seed]\n", argv[0]);
        return 1;
    }
    name = argv[1];
    if (argc > 2) cnt = atoi(argv[2]);
    if (argc > 3) lo  = atoi(argv[3]);
    if (argc > 4) hi  = atoi(argv[4]);
    if (argc > 5) len = atoi(argv[5]);
    if (argc > 6) seed = strtoul(argv[6], NULL, 10);
    else seed = (unsigned long)time(NULL) * 2654435761UL
              + (unsigned long)getpid() * 40503UL + (unsigned long)(size_t)argv[0];
    if (cnt < 1) cnt = 1;
    if (hi < lo) hi = lo;
    if (len < 1) len = 1;
    if (len > 1000) len = 1000;

    env = getenv("UPPER_SOCK");
    snprintf(path, sizeof path, "%s", (env && *env) ? env : "/tmp/upper32_a.sock");

    s = socket(AF_UNIX, SOCK_STREAM, 0);
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    strncpy(a.sun_path, path, sizeof a.sun_path - 1);
    if (s < 0 || connect(s, (void *)&a, sizeof a) < 0) { perror("connect"); return 1; }

    for (i = 1; i <= cnt; i++) {
        d = rnd(i == 1 ? 0 : lo, i == 1 ? 400 : hi);
        if (d > 0) usleep((useconds_t)d * 1000L);

        for (k = 0; k < len; k++) {                    /* новый текст каждой строки */
            char c = (char)('a' + rnd(0, 25));
            txt[k] = rnd(0, 1) ? (char)(c - 'a' + 'A') : c;
        }
        txt[len] = 0;

        snprintf(m, sizeof m, "%s#%04d sleep=%4dms %s\n", name, i, d, txt);
        if (write(s, m, strlen(m)) < 0) break;
    }
    usleep(50000);
    close(s);
    return 0;
}
