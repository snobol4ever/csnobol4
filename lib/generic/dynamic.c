/* $Id: dynamic.c,v 1.6 2020-09-27 19:45:56 phil Exp $ */

/* allocate dynamic region */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H defined */

#include <stdlib.h>			/* for malloc */

char *
dynamic(size_t size) {
    return malloc(size);
}

void
vm_gc_advise(int gc) {
}
