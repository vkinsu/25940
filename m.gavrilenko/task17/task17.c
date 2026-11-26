#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <ctype.h>
#include <string.h>

#define MAXCOL 40
#define MAXBUF 4096

/*
 * Пересчитывает начало текущего слова в буфере.
 * word_start     — индекс первого символа слова в buf[] (-1 если слова нет)
 * word_start_col — столбец, в котором это слово начинается на экране (-1 если нет)
 */
static void recompute_word(const char *buf, int len, int col,
                           int *word_start, int *word_start_col) {
    int i = len;
    while (i > 0 && buf[i - 1] != ' ') i--;
    if (i == len) {                 /* слово не найдено (все пробелы/пусто) */
        *word_start = -1;
        *word_start_col = -1;
        return;
    }
    *word_start = i;
    int c = col - (len - i);
    *word_start_col = (c < 0) ? 0 : c;
}

int main(void) {
    struct termios orig, raw;
    int use_termios = (tcgetattr(STDIN_FILENO, &orig) == 0);

    if (use_termios) {
        raw = orig;
        raw.c_lflag &= ~(ECHO | ICANON);   /* выключаем эхо и канонический режим */
        raw.c_cc[VMIN]  = 1;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }

    int erase = use_termios ? orig.c_cc[VERASE] : 0x7f;   /* Backspace */
    int kill  = use_termios ? orig.c_cc[VKILL]  : 0x15;   /* Ctrl-U */

    unsigned char ctrl_w = 0x17;   /* Ctrl-W */
    unsigned char ctrl_d = 0x04;   /* Ctrl-D */
    unsigned char ctrl_g = 0x07;   /* звонок */

    char buf[MAXBUF];
    int len = 0;                    /* сколько символов в буфере */
    int col = 0;                    /* текущий столбец на экране (0..MAXCOL) */
    int word_start = -1;            /* индекс начала текущего слова в buf */
    int word_start_col = -1;        /* столбец начала текущего слова на экране */
    int in_escape = 0;              /* внутри escape-последовательности (стрелки и т.п.) */

    while (1) {
        unsigned char c;
        if (read(STDIN_FILENO, &c, 1) != 1) break;

        /*
         * Если мы внутри escape-последовательности (стрелка, F1, Home...),
         * глотаем байты, пока не встретим завершающий (буква или '~').
         * Звонок уже выдан в момент ESC.
         */
        if (in_escape) {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '~')
                in_escape = 0;
            continue;
        }

        /* Начало escape-последовательности */
        if (c == 0x1B) {
            write(STDOUT_FILENO, &ctrl_g, 1);   /* звонок один раз */
            in_escape = 1;
            continue;
        }

        /* Ctrl-D: выход только в начале строки, иначе звонок */
        if (c == ctrl_d) {
            if (len == 0) break;
            write(STDOUT_FILENO, &ctrl_g, 1);
            continue;
        }

        /* ERASE: стереть последний символ текущей строки */
        if (c == erase) {
            if (col == 0) {
                write(STDOUT_FILENO, &ctrl_g, 1);
                continue;
            }
            len--;
            col--;
            write(STDOUT_FILENO, "\b \b", 3);
            recompute_word(buf, len, col, &word_start, &word_start_col);
            continue;
        }

        /* KILL: стереть всю текущую строку */
        if (c == kill) {
            for (int i = 0; i < col; i++)
                write(STDOUT_FILENO, "\b \b", 3);
            len = 0;
            col = 0;
            word_start = word_start_col = -1;
            continue;
        }

        /* CTRL-W: стереть последнее слово вместе с пробелами после него */
        if (c == ctrl_w) {
            while (len > 0 && buf[len - 1] == ' ') {
                len--; col--;
                write(STDOUT_FILENO, "\b \b", 3);
            }
            while (len > 0 && buf[len - 1] != ' ') {
                len--; col--;
                write(STDOUT_FILENO, "\b \b", 3);
            }
            recompute_word(buf, len, col, &word_start, &word_start_col);
            continue;
        }

        /* Enter: перевод строки, сброс */
        if (c == '\n' || c == '\r') {
            write(STDOUT_FILENO, "\n", 1);
            len = 0;
            col = 0;
            word_start = word_start_col = -1;
            continue;
        }

        /* Все непечатные, кроме обработанных выше, — звонок */
        if (!isprint(c)) {
            write(STDOUT_FILENO, &ctrl_g, 1);
            continue;
        }

        /* Пробел */
        if (c == ' ') {
            if (col >= MAXCOL) {
                write(STDOUT_FILENO, &ctrl_g, 1);
                continue;
            }
            buf[len++] = ' ';
            write(STDOUT_FILENO, &c, 1);
            col++;
            word_start = word_start_col = -1;
            continue;
        }

        /* Печатный непробельный символ */
        int at_word_start = (len == 0 || buf[len - 1] == ' ');

        if (col + 1 > MAXCOL) {
            /*
             * Символ не помещается в текущую строку.
             * Если это начало нового слова — просто переносим на новую строку.
             * Если это продолжение слова и слово началось не с 0-го столбца —
             * переносим слово целиком.
             */
            if (at_word_start) {
                write(STDOUT_FILENO, "\n", 1);
                col = 0;
                buf[len++] = c;
                write(STDOUT_FILENO, &c, 1);
                col++;
                word_start = len - 1;
                word_start_col = 0;
            } else if (word_start_col > 0) {
                int wlen = col - word_start_col;  /* длина уже выведенной части слова */
                for (int i = 0; i < wlen; i++)
                    write(STDOUT_FILENO, "\b \b", 3);
                write(STDOUT_FILENO, "\n", 1);
                for (int i = word_start; i < len; i++)
                    write(STDOUT_FILENO, &buf[i], 1);
                buf[len++] = c;
                write(STDOUT_FILENO, &c, 1);
                col = wlen + 1;
                word_start_col = 0;
            } else {
                /* слово длиннее 40 символов — печатаем дальше без переноса */
                buf[len++] = c;
                write(STDOUT_FILENO, &c, 1);
                col++;
            }
        } else {
            buf[len++] = c;
            write(STDOUT_FILENO, &c, 1);
            col++;
            if (at_word_start) {
                word_start = len - 1;
                word_start_col = col - 1;
            }
        }
    }

    if (use_termios)
        tcsetattr(STDIN_FILENO, TCSANOW, &orig);

    return 0;
}
