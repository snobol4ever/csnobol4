/* $Id: sin.c,v 1.9 2020-11-19 02:31:31 phil Exp $ */

/*
 * SPARC SPITBOL compatibility;
 * LOAD("SIN(REAL)REAL")
 * sine of angle in radians
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
SIN( LA_ALIST ) {
    (void) nargs;
    RETREAL( sin( LA_REAL(0) ) );
}
