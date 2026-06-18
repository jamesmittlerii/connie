/*****************************************************************************
 *
 *   connie_params.c
 *
 *   Drawbar and preset parameter management
 *
 *   Copyright (C) 2009,2010 Martin Homuth-Rosemann
 *
 *****************************************************************************/

#include "connie_params.h"
#include "connie.h"
#include "connie_tg.h"

#define STOPS_0 4
#define DRAWBARS_0 10
#define PRESETS_0 10

#define STOPS_1 9
#define DRAWBARS_1 12
#define PRESETS_1 10

static int ui_draw_0[DRAWBARS_0] = { 6, 8, 6, 8, 8, 4, 0, 0, 0, 4 };
static int ui_draw_1[DRAWBARS_1] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

static const int ui_preset_0[PRESETS_0][DRAWBARS_0] = {
    { 6, 8, 6, 8, 8, 4, 0, 0, 0, 4 },
    { 0, 8, 6, 8, 4, 8, 4, 0, 0, 4 },
    { 0, 8, 8, 8, 0, 8, 8, 0, 0, 4 },
    { 4, 8, 4, 6, 8, 4, 0, 1, 0, 4 },
    { 4, 8, 6, 4, 8, 0, 0, 2, 0, 4 },
    { 8, 0, 0, 0, 8, 0, 0, 4, 0, 4 },
    { 0, 8, 0, 0, 8, 0, 0, 0, 0, 4 },
    { 0, 0, 8, 0, 8, 0, 0, 0, 0, 4 },
    { 0, 0, 0, 8, 8, 0, 0, 0, 0, 4 },
    { 8, 8, 8, 8, 8, 8, 8, 4, 0, 8 }
};

static const int ui_preset_1[PRESETS_1][DRAWBARS_1] = {
    { 4, 2, 7, 8, 6, 6, 2, 4, 4, 0, 0, 4 },
    { 0, 0, 4, 5, 4, 5, 4, 4, 0, 0, 0, 4 },
    { 0, 0, 4, 4, 3, 2, 2, 2, 0, 0, 0, 4 },
    { 0, 0, 7, 3, 7, 3, 4, 3, 0, 0, 0, 4 },
    { 0, 0, 4, 5, 4, 4, 2, 2, 2, 0, 0, 4 },
    { 0, 0, 6, 6, 4, 4, 3, 2, 0, 0, 0, 4 },
    { 0, 0, 5, 6, 4, 2, 2, 0, 0, 0, 0, 4 },
    { 0, 0, 6, 8, 4, 5, 4, 3, 3, 0, 0, 4 },
    { 0, 0, 8, 0, 3, 0, 0, 0, 0, 0, 0, 4 },
    { 8, 8, 8, 8, 8, 8, 8, 8, 8, 4, 0, 8 }
};

static const char *drawbar_names_0[DRAWBARS_0] = {
  "16'", "8'", "4'", "Mixture IV", "Flute", "Reed", "Sharp",
  "Percussion", "Vibrato", "Reverb"
};

static int *ui_draw = ui_draw_0;
static int ui_drawbars = DRAWBARS_0;
static int ui_presets = PRESETS_0;
static int ui_model = CONNIE;
static int ui_program = 0;

static float squared_vol( int level, float divisor ) {
  float f = (float)level;
  return f * f / divisor;
}

static void set_volumes_0( void ) {
  tg_vol[0] = squared_vol( ui_draw[0], 64.0f );
  tg_vol[2] = squared_vol( ui_draw[1], 64.0f );
  tg_vol[3] = squared_vol( ui_draw[2], 64.0f );
  float mixture = squared_vol( ui_draw[3], 64.0f );
  tg_vol[4] = mixture;
  tg_vol[5] = mixture;
  tg_vol[6] = mixture;
  tg_vol[8] = mixture;
  tg_vol_fl     = squared_vol( ui_draw[4], 64.0f );
  tg_vol_rd     = squared_vol( ui_draw[5], 64.0f );
  tg_vol_sh     = squared_vol( ui_draw[6], 96.0f );
  tg_percussion = (float)ui_draw[7] / 8.0f;
  tg_vibrato    = (float)ui_draw[8] / 8.0f;
  tg_reverb     = squared_vol( ui_draw[9], 64.0f );
}

static void set_volumes_1( void ) {
  for ( int i = 0; i < STOPS_1; i++ )
    tg_vol[i] = squared_vol( ui_draw[i], 64.0f );
  tg_percussion = (float)ui_draw[STOPS_1] / 8.0f;
  tg_vibrato    = (float)ui_draw[STOPS_1 + 1] / 8.0f;
  tg_reverb     = squared_vol( ui_draw[STOPS_1 + 2], 64.0f );
  tg_vol_fl = 1.0f;
  tg_vol_rd = 0.0f;
  tg_vol_sh = 0.0f;
}

void connie_params_set_model( model_t model ) {
  connie_model = model;
  if ( model == HAMMOND ) {
    ui_draw = ui_draw_1;
    ui_drawbars = DRAWBARS_1;
    ui_presets = PRESETS_1;
    ui_model = HAMMOND;
  } else {
    ui_draw = ui_draw_0;
    ui_drawbars = DRAWBARS_0;
    ui_presets = PRESETS_0;
    ui_model = CONNIE;
  }
}

void connie_params_init( model_t model ) {
  connie_params_set_model( model );
  connie_params_set_program( 0 );
}

int connie_params_get_drawbar( int index ) {
  if ( index < 0 || index >= ui_drawbars )
    return 0;
  return ui_draw[index];
}

void connie_params_set_drawbar( int index, int value ) {
  if ( index < 0 || index >= ui_drawbars )
    return;
  if ( value < 0 )
    value = 0;
  if ( value > 8 )
    value = 8;
  ui_draw[index] = value;
}

void connie_params_apply_volumes( void ) {
  if ( ui_model == HAMMOND )
    set_volumes_1();
  else
    set_volumes_0();
}

int connie_params_set_program( int prog ) {
  if ( prog < 0 || prog >= ui_presets )
    return -1;
  ui_program = prog;
  switch ( ui_model ) {
    case CONNIE:
      for ( int i = 0; i < DRAWBARS_0; i++ )
        ui_draw[i] = ui_preset_0[prog][i];
      break;
    case HAMMOND:
      for ( int i = 0; i < DRAWBARS_1; i++ )
        ui_draw[i] = ui_preset_1[prog][i];
      break;
    default:
      break;
  }
  connie_params_apply_volumes();
  return 0;
}

int connie_params_get_program( void ) {
  return ui_program;
}

const char *connie_params_drawbar_name( int index ) {
  if ( index < 0 || index >= DRAWBARS_0 )
    return "";
  return drawbar_names_0[index];
}
