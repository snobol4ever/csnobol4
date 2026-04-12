/* $Id: term.c,v 1.2 2020-09-27 22:18:09 phil Exp $ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H defined */

#include <stdio.h>

#include "h.h"
#include "snotypes.h"
#include "lib.h"

/*
 * return a stdio stream for TERMINAL input variable
 * Win32 version.
 */

FILE *
term_input(void) {
    return fopen("CONIN$", "r");
}
