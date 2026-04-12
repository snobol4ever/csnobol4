/* $Id: tty.c,v 1.9 2024-09-17 20:53:20 phil Exp $ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H defined */

#include <stdio.h>

#include "h.h"
#include "snotypes.h"
#include "lib.h"

/*
 * tty mode, echo
 * dummy version
 */

int
fisatty(FILE *f) {
    return FALSE;			/* why not? */
}

void
tty_mode(FILE *fp, int cbreak, int noecho, int recl) {
}

/* advisory notice */
void
tty_close(FILE *f) {
    /* should not be called (fisatty returns FALSE) */
}

void
tty_suspend(void) {
}
