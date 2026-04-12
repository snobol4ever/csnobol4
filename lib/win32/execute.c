/* $Id: execute.c,v 1.2 2020-10-07 03:17:47 phil Exp $ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H defined */

#include <process.h>			/* execl */
#include <stdio.h>			/* NULL */
#include <stdlib.h>			/* getenv */

#include "h.h"
#include "snotypes.h"
#include "lib.h"

void
execute(char *buf) {
    char *cmd = getenv("COMSPEC");
    if (!cmd)
	cmd = "C:\\Windows\\System32\\cmd.exe";
    execl(cmd, "cmd", "/c", buf, NULL);
}
