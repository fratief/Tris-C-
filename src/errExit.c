/************************************
VR486336,VR487324,VR486827
FRANCESCO TIEFENTHALER, DENIS GALLO, RAKIB HAQUE
17/06/2024
*************************************/



#include "../inc/errExit.h"

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

void errExit(const char *msg) {
    perror(msg);
    exit(1);
}