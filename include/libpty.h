/* $Id: libpty.h,v 1.1 2020-10-08 18:39:18 phil Exp $ */

#include <termios.h>

/* lib/posix/pty.c: */
extern int forkpty(int *amaster, char *name, struct termios *termp, struct winsize *winp);
