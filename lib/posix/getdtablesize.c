/* $Id: getdtablesize.c,v 1.2 2020-09-27 22:08:32 phil Exp $ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H defined */

#include <unistd.h>
#include <stdio.h>			/* for lib.h */

#include "h.h"
#include "snotypes.h"
#include "lib.h"

int
getdtablesize(void) {
    return sysconf(_SC_OPEN_MAX);
}
