#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <unistd.h>
#include <inttypes.h>

static char get_type(mode_t m) {
    if (S_ISDIR(m))  return 'd';
    if (S_ISREG(m))  return '-';
    return '?';
}

static void get_perms(mode_t m, char p[10]) {
    p[0] = (m & S_IRUSR) ? 'r' : '-';
    p[1] = (m & S_IWUSR) ? 'w' : '-';
    p[2] = (m & S_IXUSR) ? 'x' : '-';
    p[3] = (m & S_IRGRP) ? 'r' : '-';
    p[4] = (m & S_IWGRP) ? 'w' : '-';
    p[5] = (m & S_IXGRP) ? 'x' : '-';
    p[6] = (m & S_IROTH) ? 'r' : '-';
    p[7] = (m & S_IWOTH) ? 'w' : '-';
    p[8] = (m & S_IXOTH) ? 'x' : '-';
    p[9] = '\0';
}

static const char *my_basename(const char *path) {
    const char *end = path + strlen(path);
    while (end > path + 1 && end[-1] == '/') end--;   /* strip trailing / */
    const char *start = end;
    while (start > path && start[-1] != '/') start--;
    static char tmp[4096];
    size_t n = (size_t)(end - start);
    if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
    memcpy(tmp, start, n);
    tmp[n] = '\0';
    return tmp;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <path>...\n", argv[0]);
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        struct stat st;
        if (lstat(argv[i], &st) == -1) {
            perror(argv[i]);
            continue;
        }
        char perms[10];
        get_perms(st.st_mode, perms);

        struct passwd *pw = getpwuid(st.st_uid);
        struct group  *gr = getgrgid(st.st_gid);

        char datebuf[64];
        struct tm *tm = localtime(&st.st_mtime);
        strftime(datebuf, sizeof(datebuf), "%b %e %H:%M", tm);

        printf("%c%s %3ju %-8s %-8s ",
            get_type(st.st_mode), perms,
            (uintmax_t)st.st_nlink,
            pw ? pw->pw_name : "?",
            gr ? gr->gr_name : "?");

        if (S_ISREG(st.st_mode))
            printf("%8lld ", (long long)st.st_size);
        else
            printf("%8s ", "");

        printf("%s %s\n", datebuf, my_basename(argv[i]));
    }
    return 0;
}