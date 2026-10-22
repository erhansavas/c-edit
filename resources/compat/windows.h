/* The subset of <windows.h> used by console programs. The macOS console is UTF-8, so code-page calls succeed as no-ops. */
#ifndef CEDIT_WINDOWS_H
#define CEDIT_WINDOWS_H
#include <unistd.h>
typedef int BOOL;
typedef unsigned int UINT;
typedef unsigned long DWORD;
#define TRUE 1
#define FALSE 0
static inline BOOL SetConsoleOutputCP(UINT code_page) { (void)code_page; return TRUE; }
static inline BOOL SetConsoleCP(UINT code_page) { (void)code_page; return TRUE; }
static inline void Sleep(DWORD milliseconds) { usleep((useconds_t)milliseconds * 1000); }
#endif
