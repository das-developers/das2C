/*
 * File:   debug.c.c
 * Author: Jeremy
 *
 * Created on January 29, 2004, 2:51 PM
 */

#include <stdio.h>
#include <stdlib.h>

#include "Das2Stream.h"

void dbg_enter( char * name ) {
    fprintf( stderr, "Enter:%s:\n", name );
}

void dbg_probe_float( char * name, float value ) {
    fprintf( stderr, "Probe:%s:%f:\n", name, value );
}

void dbg_probe_double( char * name, double value ) {
    fprintf( stderr, "Probe:%s:%f:\n", name, value );
}

void dbg_probe_int( char * name, int value ) {
    fprintf( stderr, "Probe:%s:%d:\n", name, value );
}

void dbg_probe_string( char * name, char * value ) {
    fprintf( stderr, "Probe:%s:%s:\n", name, value );
}

void dbg_probe_StreamInputDescriptor( char * name, StreamInputDescriptor sid ) {
    fprintf( stderr, "Probe:%s:mode=%d buffer=%zd:\n", name, sid->mode, (long)sid->sBuffer );
}
