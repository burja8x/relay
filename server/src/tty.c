/*
Parts of the code (pty) are from "Advanced Programming in the Unix Environment" 
http://www.apuebook.com/code3e.html
*/
#include <errno.h>
#include <termios.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "log.h"

static int size_rows = 30;
static int size_cols = 120;
static int fdm;


bool prefix(const char *pre, const char *str) {
  return strncmp(pre, str, strlen(pre)) == 0;
}
char *substring(char *destination, const char *source, int beg, int n) {
  while (n > 0) {
    *destination = *(source + beg);

    destination++;
    source++;
    n--;
  }
  *destination = '\0';
  return destination;
}

void parseSize(const char *str) {
  char rows_str[7];
  char cols_str[7];
  substring(cols_str, str, 8, 6);
  substring(rows_str, str, 17, 6);

  Log(INFO, "new size: rows:'%s'  cols:'%s'", rows_str, cols_str);
  fflush(stdout);
  int row_i = atoi(rows_str);
  int col_i = atoi(cols_str);

  if (row_i > 2 && col_i > 6) {
    size_rows = row_i;
    size_cols = col_i;
  }
  // printf("%d %d\n", size_rows, size_cols);
  fflush(stdout);
}

ssize_t writen(int fd, const void *ptr, size_t n){
	size_t		nleft;
	ssize_t		nwritten;

	nleft = n;
	while (nleft > 0) {
		if ((nwritten = write(fd, ptr, nleft)) < 0) {
			if (nleft == n)
				return(-1); /* error, return -1 */
			else
				break;      /* error, return amount written so far */
		} else if (nwritten == 0) {
			break;
		}
		nleft -= nwritten;
		ptr   += nwritten;
	}
	return(n - nleft);      /* return >= 0 */
}


int ptym_open(char *pts_name, int pts_namesz){
	char	*ptr;
	int		fdm, err;

	if ((fdm = posix_openpt(O_RDWR)) < 0)
		return(-1);
	if (grantpt(fdm) < 0)		/* grant access to slave */
		goto errout;
	if (unlockpt(fdm) < 0)		/* clear slave's lock flag */
		goto errout;
	if ((ptr = ptsname(fdm)) == NULL)	/* get slave's name */
		goto errout;

	/*
	 * Return name of slave.  Null terminate to handle
	 * case where strlen(ptr) > pts_namesz.
	 */
	strncpy(pts_name, ptr, pts_namesz);
	pts_name[pts_namesz - 1] = '\0';
	return(fdm);			/* return fd of master */
errout:
	err = errno;
	close(fdm);
	errno = err;
	return(-1);
}

int ptys_open(char *pts_name){
	int fds;

	if ((fds = open(pts_name, O_RDWR)) < 0)
		return(-1);
	return(fds);
}

pid_t pty_fork_new(int *ptrfdm)
{
	int		fdm, fds;
	pid_t	pid;
	char	pts_name[20];
	errno = 0;
	if ((fdm = ptym_open(pts_name, sizeof(pts_name))) < 0){
        Log(ERROR, "can't open master pty: %s, error %d, %s", pts_name, fdm, strerror(errno));
        exit(1);
    }
	errno = 0;
	if ((pid = fork()) < 0) {
		return(-1);
	} else if (pid == 0) {		/* child */
		if (setsid() < 0)
			Log(ERROR ,"setsid error %s", strerror(errno));

		/*
		 * System V acquires controlling terminal on open().
		 */
		errno = 0;
		if ((fds = ptys_open(pts_name)) < 0)
			Log(ERROR, "can't open slave pty %s", strerror(errno));
		close(fdm);		/* all done with master in child */

		/*
		 * Slave becomes stdin/stdout/stderr of child.
		 */
		errno = 0;
		if (dup2(fds, STDIN_FILENO) != STDIN_FILENO)
			Log(ERROR, "dup2 error to stdin %s", strerror(errno));
		if (dup2(fds, STDOUT_FILENO) != STDOUT_FILENO)
			Log(ERROR, "dup2 error to stdout %s", strerror(errno));
		if (dup2(fds, STDERR_FILENO) != STDERR_FILENO)
			Log(ERROR, "dup2 error to stderr %s", strerror(errno));
		if (fds != STDIN_FILENO && fds != STDOUT_FILENO && fds != STDERR_FILENO)
			close(fds);
		return(0);		/* child returns 0 just like fork() */
	} else {					/* parent */
		*ptrfdm = fdm;	/* return fd of master */
		return(pid);	/* parent returns pid of child */
	}
}

int tty_new() {
	pid_t pid = pty_fork_new(&fdm);
	if (pid < 0) {
		Log(ERROR, "pid < 0 ... pty_fork_new(%d, )", fdm);
	} else if (pid == 0) { /* child */
		Log(INFO, "Run tmux new");
		errno = 0;
		char *argument_list[] = {"tmux", "new", "-As0", NULL};
		if (execvp(argument_list[0], argument_list) < 0){
			Log(ERROR, "can't execute: tmux  %s", strerror(errno));
			exit(1);
		}
		Log(DEBUG, "tty_new()  pid == 0");
		printf("exit child !");
		exit(1);
	}

	// non blocking .... becouse terminal block as (read fun.)
	int oldflags = fcntl(fdm, F_GETFL, 0);
	oldflags |= O_NONBLOCK;
	fcntl(fdm, F_SETFL, oldflags);

	Log(DEBUG, "tty_new()  pid:%d", pid);
	return fdm;
}

void changeWindowSize(const char *str){
    parseSize(str);
	struct winsize size;
	size.ws_row = size_rows;
	size.ws_col = size_cols;
	errno = 0;
	if (ioctl(fdm, TIOCSWINSZ, &size) < 0)
		Log(ERROR, "ioctl(%d) ... TIOCSWINSZ ... setting window size. %s", fdm, strerror(errno));
}

bool ExeCmd(char * cmd, bool show){
	if(show){
		Log(INFO, "exe:'%s'", cmd);
		Log(INFO, "IF NOTHING HAPPENS check if Proxmark is connected...", cmd);
	}
	errno = 0;
	int ret = writen(fdm, cmd, strlen(cmd)); // write limit !?
	if (ret != (ssize_t)strlen(cmd)){
		Log(ERROR, "writen(%d) return (%d) when executing '%s' error:%s", fdm, ret, cmd, strerror(errno));
		return false;
	}
	return true;
}
bool ExeCmdL(const char * cmd, int len, bool show){
	if(show){
		Log(INFO, "exe:'%s'", cmd);
	}
	errno = 0;
	int ret = writen(fdm, cmd, len); // write limit !?
	if (ret != len){
		Log(ERROR, "writen(%d) return (%d) when executing '%s' with length:%d error:%s", fdm, ret, cmd, len, strerror(errno));
		return false;
	}
	return true;
}

bool ExeCtrlC(){
	char ctrlc[1];
	ctrlc[0] = 20; // 2,3,20
	errno = 0;
	int ret = writen(fdm, ctrlc, 1);
	if (ret != 1){
		Log(ERROR, "writen(%d) return (%d) when executing ( Ctrl + C ) error:%s", fdm, ret, strerror(errno));
		return false;
	}
	return true;
}