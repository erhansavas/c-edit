/* Force-included into every program C Editor compiles: maps the Windows console idioms that code
   written on Windows relies on onto their macOS equivalents. */
#ifndef CEDIT_COMPAT_H
#define CEDIT_COMPAT_H
#include <fcntl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <termios.h>
#include <unistd.h>

/* Windows locale names become macOS UTF-8 locales: Turkish ones ("Turkish", "Turkish_Turkey.1254", "tr-TR",
   ...) the Turkish locale, English ones ("English", "English_United States.1252", ...) the English one.
   Other names macOS does not know fall back to the Turkish locale programs run with. */
static inline char *cedit_setlocale(int category, const char *locale) {
    if (locale && (strncasecmp(locale, "turk", 4) == 0 || strncasecmp(locale, "tr", 2) == 0 ||
                   strstr(locale, "1254") || strstr(locale, "857")))
        locale = "tr_TR.UTF-8";
    else if (locale && (strncasecmp(locale, "english", 7) == 0 || strncasecmp(locale, "en", 2) == 0))
        locale = "en_US.UTF-8";
    char *result = setlocale(category, locale);
    return result ? result : setlocale(category, "");
}
#define setlocale(category, locale) cedit_setlocale((category), (locale))

static inline void cedit_wait_key(void) {
    int fd = open("/dev/tty", O_RDONLY);
    if (fd < 0) return;
    struct termios saved, raw;
    tcgetattr(fd, &saved);
    raw = saved;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    tcsetattr(fd, TCSANOW, &raw);
    char key;
    (void)read(fd, &key, 1);
    tcflush(fd, TCIFLUSH); /* the rest of a key that sends several bytes: arrows, Turkish letters */
    tcsetattr(fd, TCSANOW, &saved);
    close(fd);
}

/* cmd.exe built-ins: chcp/title/mode/color are no-ops (the console is UTF-8), cls clears, pause waits. */
static inline int cedit_system(const char *command) {
    if (!command) return 1;
    while (*command == ' ' || *command == '@') command++;
    if (strncasecmp(command, "chcp", 4) == 0 || strncasecmp(command, "title", 5) == 0 ||
        strncasecmp(command, "mode", 4) == 0 || strncasecmp(command, "color", 5) == 0)
        return 0;
    if (strncasecmp(command, "cls", 3) == 0) {
        fputs("\033[2J\033[H", stdout);
        fflush(stdout);
        return 0;
    }
    if (strncasecmp(command, "pause", 5) == 0) {
        fflush(stdout);
        if (!strstr(command, "nul")) {
            fputs("Devam etmek için bir tuşa basın . . . ", stdout);  /* Turkish Windows' own words */
            fflush(stdout);
        }
        cedit_wait_key();
        putchar('\n');
        return 0;
    }
    return system(command);
}
#define system(command) cedit_system(command)

/* fflush(stdin) discards the rest of the typed line, as the Windows C runtime (msvcrt) does. */
static inline int cedit_fflush(FILE *stream) { return stream == stdin ? fpurge(stream) : fflush(stream); }
#define fflush(stream) cedit_fflush(stream)

#ifdef __cplusplus
/* std::system("pause") and friends reach the same replacements. */
namespace std { using ::cedit_fflush; using ::cedit_setlocale; using ::cedit_system; }
#endif
#endif
