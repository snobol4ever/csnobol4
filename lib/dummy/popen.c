/* $Id: popen.c,v 1.3 2024-09-17 20:53:20 phil Exp $ */
 
/*
 * dummy popen()/pclose()
 * September 24, 1997
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H defined */

#include <stdio.h>

FILE *
popen(char *file, char *mode) {
    return NULL;
}

int
pclose(FILE *f) {
    return -1;
}
