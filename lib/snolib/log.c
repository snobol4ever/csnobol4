/* $Id: log.c,v 1.8 2020-11-19 02:31:31 phil Exp $ */

/*
 * SNOBOL4+ compatibility;
 * LOAD("LOG(REAL)REAL")
 * natural logarithm
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H defined */

#include <math.h>

#include "h.h"
#include "snotypes.h"
#include "macros.h"
#include "load.h"
#include "equ.h"

pmlret_t
LOG( LA_ALIST ) {
    (void) nargs;
    RETREAL( log( LA_REAL(0) ) );
}
