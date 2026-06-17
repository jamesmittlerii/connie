/*****************************************************************************
 *
 *   headless_dsp_test.c
 *
 *   DSP smoke test without an LV2 host
 *
 *****************************************************************************/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "connie.h"
#include "connie_dsp.h"
#include "connie_params.h"
#include "connie_tg.h"

static float buffer_energy( const float *buf, int n ) {
  float acc = 0.0f;
  for ( int i = 0; i < n; i++ ) {
    float v = buf[i];
    acc += v < 0.0f ? -v : v;
  }
  return acc;
}

static float render_energy( int frames, const connie_midi_event_t *events, int num_events ) {
  float *left = (float *)calloc( (size_t)frames, sizeof( float ) );
  float *right = (float *)calloc( (size_t)frames, sizeof( float ) );
  if ( !left || !right ) {
    free( left );
    free( right );
    return 0.0f;
  }
  connie_dsp_process( left, right, frames, events, num_events );
  float e = buffer_energy( left, frames ) + buffer_energy( right, frames );
  free( left );
  free( right );
  return e;
}

int main( void ) {
  connie_params_init( CONNIE );
  connie_dsp_init( 48000 );
  connie_params_set_drawbar( 1, 8 );
  connie_params_set_drawbar( 4, 8 );
  connie_params_apply_volumes();
  tg_master_vol = 1.0f;

  connie_dsp_panic();
  float silent = render_energy( 1024, NULL, 0 );
  if ( silent > 1e-3f ) {
    fprintf( stderr, "FAIL: expected silence after panic, got energy=%g\n", silent );
    connie_dsp_shutdown();
    return EXIT_FAILURE;
  }

  connie_midi_event_t note_on = { 0, { 0x90, 60, 127 }, 3 };
  float on_energy = render_energy( 1024, &note_on, 1 );
  if ( on_energy <= 1e-4f ) {
    fprintf( stderr, "FAIL: expected audio for note on, got %g\n", on_energy );
    connie_dsp_shutdown();
    return EXIT_FAILURE;
  }

  tg_master_vol = 0.2f;
  float low = render_energy( 1024, &note_on, 1 );
  tg_master_vol = 1.0f;
  float high = render_energy( 1024, &note_on, 1 );
  if ( high <= low * 1.5f ) {
    fprintf( stderr, "FAIL: master volume ineffective (low=%g high=%g)\n", low, high );
    connie_dsp_shutdown();
    return EXIT_FAILURE;
  }

  connie_dsp_shutdown();
  printf( "headless-dsp-test: PASS\n" );
  return EXIT_SUCCESS;
}
