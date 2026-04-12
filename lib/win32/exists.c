/* $Id: exists.c,v 1.10 2020-11-02 23:22:43 phil Exp $ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H defined */

#include <windows.h>
#include <stdio.h>
#include <ctype.h>

#include "h.h"
#include "snotypes.h"
#include "lib.h"

int
exists(char *path) {
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

int
isdir(char *path) {
    DWORD attr = GetFileAttributesA(path);

    return (attr != INVALID_FILE_ATTRIBUTES && 
	    (attr & FILE_ATTRIBUTE_DIRECTORY));
}

int
abspath(char *path) {
#define BS DIR_SEP[0]
    return (*path == '/' ||
	    *path == BS ||
	    (isalpha((unsigned char)*path) &&
	     path[1] == ':' && path[2] == BS));
}
