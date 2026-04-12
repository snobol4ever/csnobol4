/* $Id: suspend.c,v 1.5 2020-09-27 22:15:22 phil Exp $ */

/*
 * process suspend
 * generic version
 */

/* not needed (obviously), but forces remake when re-configured */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H defined */

#include <signal.h>
#include <stdio.h>

#include "h.h"
#include "snotypes.h"
#include "lib.h"

void
proc_suspend(void) {
    kill(getpid(), SIGSTOP);		/* use 0 for process group? */
}
