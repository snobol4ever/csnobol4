/*
 * $Id: unlink.c,v 1.2 2020-09-29 05:02:39 phil Exp $
 * Needed under VAXC v3
 */

int
unlink(char *fname) {
    return delete(fname);
}
