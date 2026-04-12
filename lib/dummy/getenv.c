/* $Id: getenv.c,v 1.2 2024-09-17 20:53:20 phil Exp $ */

/* dummy getenv() function for snolib/host.c */

char *
getenv(char *str) {
    return (char *)0;			/* NULL */
}
