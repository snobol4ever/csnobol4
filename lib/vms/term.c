/* $Id: term.c,v 1.8 2020-09-29 05:02:39 phil Exp $ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H defined */

#include <stdio.h>

#include "h.h"
#include "snotypes.h"
#include "lib.h"

/*
 * return a stdio stream for TERMINAL input variable
 * VMS version.
 */

FILE *
term_input(void) {
    return fopen("SYS$COMMAND:", "r");
}
