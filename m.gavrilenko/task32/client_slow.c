#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#define SOCK_PATH "/tmp/upper32.sock"

int main(void) {
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s == -1) { perror("socket"); return 1; }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path) - 1);
    if (connect(s, (struct sockaddr *)&addr, sizeof addr) == -1) {
        perror("connect"); return 1;
    }
    for (int i = 0; i < 5; i++) {
        char line[64];
        int len = snprintf(line, sizeof line, "slow line %d\n", i);
        write(s, line, len);
        sleep(1);
    }
    close(s);
    return 0;
}
