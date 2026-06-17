/*****************************************************************************
 *
 *   connie_params.h
 *
 *   Drawbar and preset parameter management
 *
 *   Copyright (C) 2009,2010 Martin Homuth-Rosemann
 *
 *****************************************************************************/
#ifndef CONNIE_PARAMS_H
#define CONNIE_PARAMS_H

#include "connie.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONNIE_NUM_DRAWBARS 10
#define CONNIE_NUM_PRESETS  10

void connie_params_init( model_t model );
void connie_params_set_model( model_t model );

int connie_params_get_drawbar( int index );
void connie_params_set_drawbar( int index, int value );
void connie_params_apply_volumes( void );

int connie_params_set_program( int prog );
int connie_params_get_program( void );

const char *connie_params_drawbar_name( int index );

#ifdef __cplusplus
}
#endif

#endif
