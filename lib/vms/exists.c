/* $Id: exists.c,v 1.4 2020-09-29 05:02:39 phil Exp $ */

/*
 * File existance check for VMS
 * P. Budne Feb 10, 1998
 */

/* XXX use a native call?? */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H defined */

#include <stat.h>
#include <stdio.h>

#include "h.h"
#include "snotypes.h"
#include "lib.h"

int
exists(char *path) {
    struct stat st;

    return stat(path, &st) >= 0;
}
