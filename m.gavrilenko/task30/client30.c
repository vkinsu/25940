#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#define SOCK_PATH "/tmp/upper30.sock"

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

    const char *msg =
        "Hello World!\n"
        "MiXeD CaSe Text 123\n"
        "abcdefghij\n";

    if (write(s, msg, strlen(msg)) == -1) { perror("write"); return 1; }
    close(s);
    return 0;
}
