/*****************************************************************************
 *
 *   connie_dsp.h
 *
 *   Connie organ DSP engine (host-independent)
 *
 *   Copyright (C) 2009,2010 Martin Homuth-Rosemann
 *
 *****************************************************************************/
#ifndef CONNIE_DSP_H
#define CONNIE_DSP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t sample_offset;
  uint8_t  data[3];
  int32_t  size;
} connie_midi_event_t;

void connie_dsp_init( int sample_rate );
void connie_dsp_shutdown( void );

void connie_dsp_process(
  float *out_l,
  float *out_r,
  int32_t num_frames,
  const connie_midi_event_t *events,
  int32_t num_events
);

void connie_dsp_panic( void );

void connie_dsp_midi( const uint8_t *data, int32_t size );

#ifdef __cplusplus
}
#endif

#endif
