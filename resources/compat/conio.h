/* <conio.h> key reads on the C Editor console. */
#ifndef CEDIT_CONIO_H
#define CEDIT_CONIO_H
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
static inline int cedit_getch(int echo) {
    struct termios saved, raw;
    fflush(stdout);
    tcgetattr(0, &saved);
    raw = saved;
    raw.c_lflag &= (tcflag_t)~(ICANON | (echo ? 0 : ECHO));
    tcsetattr(0, TCSANOW, &raw);
    int key = getchar();
    tcsetattr(0, TCSANOW, &saved);
    return key;
}
static inline int getch(void) { return cedit_getch(0); }
static inline int getche(void) { return cedit_getch(1); }
#define _getch getch
#define _getche getche
#endif
