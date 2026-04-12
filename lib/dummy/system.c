/* $Id: system.c,v 1.2 2024-09-17 20:53:20 phil Exp $ */

/* dummy system() function for snolib/host.c */

int
system(char *cmd) {
    return -1;
}
