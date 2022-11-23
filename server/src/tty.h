#include <stdio.h>
#include <stdbool.h>

// char *strerror(int error);
// static void err_doit(int errnoflag, int error, const char *fmt, va_list ap);
// void err_sys(const char *fmt, ...);
void err_sys(const char* x);
bool prefix(const char *pre, const char *str);
void parseSize(const char *str);
char *substring(char *destination, const char *source, int beg, int n);
int ptym_open(char *pts_name, int pts_namesz);
int ptys_open(char *pts_name);
pid_t pty_fork_new(int *ptrfdm);
int tty_new();
void changeWindowSize(const char *str);
ssize_t writen(int fd, const void *ptr, size_t n);
bool ExeCtrlC();
bool ExeCmd(char * cmd, bool show);
bool ExeCmdL(const char * cmd, int len, bool show);