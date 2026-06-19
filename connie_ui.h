/*****************************************************************************
 *
 *   connie_ui.h
 *
 *   Simulation of an electronic organ like Vox Continental
 *   with JACK MIDI input and JACK audio output
 *
 *   Copyright (C) 2009,2010 Martin Homuth-Rosemann
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *****************************************************************************/
#ifndef CONNIE_UI_H
#define CONNIE_UI_H

typedef enum keybd_enum { QWERTY=0, QWERTZ, AZERTY } keybd_t;

extern int ui_set_program( int prog );
extern int ui_set_drawbars( const int *draw );
extern void ui_save( int type, const char *path );
extern void ui_init( const int model, const keybd_t keybd );
extern void ui_loop( const char *name );

#endif
