/*****************************************************************************
 *
 *   connie_dsp.c
 *
 *   Connie organ DSP engine (host-independent)
 *
 *   Copyright (C) 2009,2010 Martin Homuth-Rosemann
 *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "connie.h"
#include "connie_dsp.h"
#include "connie_params.h"
#include "connie_tuning.h"
#include "reverb.h"
#include "scales.h"

#define VIBRATO 6.4f

#define MIDI_MAX 128
#define OCT_SAMP (OCTAVES + 2)
#define OCT_MIX (OCTAVES + 3)
#define NOTE_MAX (LOWNOTE + 12 * OCT_MIX)
#define OCT 12
#define FIFTH 7
#define THIRD 4

const int TG_STEP = 8;
const float tg_halftone = 1.059463094f;

int intonation = 0;
float concert_pitch = 440.0f;
int transpose = 0;
const char *inton_name;
model_t connie_model = CONNIE;
int tg_midi_channel = 0;

typedef float sample_t;

static int tg_sample_rate;

static sample_t *tg_cycle_fl = NULL;
static sample_t *tg_cycle_rd[OCT_SAMP];
static sample_t *tg_cycle_sh[OCT_SAMP];
static int tg_sam_in_cy;
static float tg_midi_freq[MIDI_MAX];
static float tg_sample_offset[12];

static int midi_vol_raw[MIDI_MAX];
static int midi_vol_smooth[MIDI_MAX];
static int tg_vol_key[MIDI_MAX];
static int tg_vol_note[NOTE_MAX];
static int midi_cc[128];
static int midi_pitch = 0;
static int midi_prog = 0;

float tg_vibrato = 0;
float tg_percussion = 0;
float tg_reverb = 0;
float tg_vol[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
float tg_vol_fl = 0;
float tg_vol_rd = 0;
float tg_vol_sh = 0;
float tg_master_vol = 0.25f;

#define VOL_RAW_MAX 1000
static int soft_step[2 * VOL_RAW_MAX + 1];

void connie_dsp_panic( void ) {
  for ( int iii = 0; iii < MIDI_MAX; iii++ ) {
    midi_vol_raw[iii] = 0;
    tg_vol_key[iii] = 0;
  }
  for ( int iii = 0; iii < NOTE_MAX; iii++ )
    tg_vol_note[iii] = 0;
}

void tg_panic( void ) {
  connie_dsp_panic();
}

static sample_t clip( sample_t sample ) {
  if ( sample > 1.0f )
    sample = 2.0f / 3.0f;
  else if ( sample < -1.0f )
    sample = -2.0f / 3.0f;
  else
    sample = sample - (sample * sample * sample) / 3.0f;
  return sample;
}

static sample_t getsample( unsigned int tone, unsigned int octave ) {
  float foldback_damp = 1.f;
  while ( tone >= 12 ) {
    tone -= 12;
    octave++;
  }
  while ( octave >= OCT_SAMP ) {
    octave--;
    foldback_damp *= 1.5f;
  }
  unsigned int pos = (unsigned int)(tg_sample_offset[tone] * (float)(1 << octave));
  while ( pos >= (unsigned int)tg_sam_in_cy )
    pos -= (unsigned int)tg_sam_in_cy;

  sample_t sample = tg_cycle_fl[pos] * tg_vol_fl;

  if ( CONNIE == connie_model ) {
    if ( tg_vol_rd != 0.0f ) {
      if ( octave > 0 && tone < 4 ) {
        sample += ((float)(4 - tone) * tg_cycle_rd[octave - 1][pos]
                 + (float)(4 + tone) * tg_cycle_rd[octave][pos]) * tg_vol_rd / 8.0f;
      } else if ( octave < OCT_SAMP - 1 && tone > 7 ) {
        sample += ((float)(11 + 4 - tone) * tg_cycle_rd[octave][pos]
                 + (float)(tone - (11 - 4)) * tg_cycle_rd[octave + 1][pos]) * tg_vol_rd / 8.0f;
      } else {
        sample += tg_cycle_rd[octave][pos] * tg_vol_rd;
      }
    }
    if ( tg_vol_sh != 0.0f ) {
      if ( octave > 0 && tone < 4 ) {
        sample += ((float)(4 - tone) * tg_cycle_sh[octave - 1][pos]
                 + (float)(4 + tone) * tg_cycle_sh[octave][pos]) * tg_vol_sh / 8.0f;
      } else if ( octave < OCT_SAMP - 1 && tone > 7 ) {
        sample += ((float)(11 + 4 - tone) * tg_cycle_sh[octave][pos]
                 + (float)(tone - (11 - 4)) * tg_cycle_sh[octave + 1][pos]) * tg_vol_sh / 8.0f;
      } else {
        sample += tg_cycle_sh[octave][pos] * tg_vol_sh;
      }
    }
  }
  return sample / foldback_damp;
}

static int transpose_note( int note ) {
  note += transpose;
  if ( note < LOWNOTE || note > HIGHNOTE )
    return 0;
  return note;
}

static void handle_midi_bytes( const uint8_t *buf, int size ) {
  if ( size == 3 ) {
    int note;
    if ( (buf[0] >> 4) == 0x08 ) {
      note = transpose_note( buf[1] );
      midi_vol_raw[note] = 0;
    } else if ( (buf[0] >> 4) == 0x09 ) {
      note = transpose_note( buf[1] );
      midi_vol_raw[note] = buf[2] ? VOL_RAW_MAX : 0;
    } else if ( (buf[0] >> 4) == 0x0B ) {
      int cc = buf[1];
      midi_cc[cc] = buf[2];
      if ( cc == 7 )
        tg_master_vol = buf[2] * buf[2] / 127.0f / 127.0f;
      else if ( cc == 120 || cc == 123 )
        connie_dsp_panic();
    } else if ( (buf[0] >> 4) == 0x0E ) {
      midi_pitch = 128 * buf[2] + buf[1] - 0x2000;
    }
  } else if ( size == 2 && (buf[0] >> 4) == 0x0C ) {
    midi_prog = buf[1];
    connie_params_set_program( midi_prog );
  }
}

void connie_dsp_midi( const uint8_t *data, int32_t size ) {
  if ( data && size > 0 )
    handle_midi_bytes( data, size );
}

static int count_active_keys( void ) {
  int act_keys = 0;
  for ( int note = LOWNOTE; note < HIGHNOTE; note++ )
    act_keys += midi_vol_raw[note] != 0;
  return act_keys;
}

static void smooth_one_note( int *p_smooth, const int *p_raw, int *p_vol, int step, int act_keys ) {
  if ( *p_smooth < *p_raw ) {
    if ( tg_percussion != 0.0f && 1 == act_keys && 0 == *p_smooth )
      *p_smooth = (int)(2 * VOL_RAW_MAX * tg_percussion);
    else
      *p_smooth += 5 * step;
  } else if ( *p_smooth > *p_raw ) {
    *p_smooth -= step;
  }
  *p_vol = soft_step[*p_smooth];
}

static void update_volumes( void ) {
  int *p_vol = tg_vol_key + LOWNOTE;
  const int *p_raw = midi_vol_raw + LOWNOTE;
  int *p_smooth = midi_vol_smooth + LOWNOTE;
  int act_keys = tg_percussion != 0.0f ? count_active_keys() : 0;

  for ( int octave = 0, step = 1; octave < OCTAVES; octave++, step *= 2 ) {
    for ( int note = 0; note < 12; note++, p_vol++, p_raw++, p_smooth++ )
      smooth_one_note( p_smooth, p_raw, p_vol, step, act_keys );
  }

  for ( int note = 0; note < NOTE_MAX; note++ )
    tg_vol_note[note] = 0;

  const int *p_key = tg_vol_key + LOWNOTE;
  int *p_16  = tg_vol_note + LOWNOTE - OCT;
  int *p_513 = tg_vol_note + LOWNOTE + FIFTH;
  int *p_8   = tg_vol_note + LOWNOTE;
  int *p_4   = tg_vol_note + LOWNOTE + OCT;
  int *p_223 = tg_vol_note + LOWNOTE + OCT + FIFTH;
  int *p_2   = tg_vol_note + LOWNOTE + OCT + OCT;
  int *p_135 = tg_vol_note + LOWNOTE + OCT + OCT + THIRD;
  int *p_113 = tg_vol_note + LOWNOTE + OCT + OCT + FIFTH;
  int *p_1   = tg_vol_note + LOWNOTE + OCT + OCT + OCT;

  for ( int key = LOWNOTE; key < HIGHNOTE; key++ ) {
    if ( *p_key ) {
      const float *p_v = tg_vol;
      *p_16  += (int)((float)*p_key * *p_v++);
      *p_513 += (int)((float)*p_key * *p_v++);
      *p_8   += (int)((float)*p_key * *p_v++);
      *p_4   += (int)((float)*p_key * *p_v++);
      *p_223 += (int)((float)*p_key * *p_v++);
      *p_2   += (int)((float)*p_key * *p_v++);
      *p_135 += (int)((float)*p_key * *p_v++);
      *p_113 += (int)((float)*p_key * *p_v++);
      *p_1   += (int)((float)*p_key * *p_v++);
    }
    p_key++;
    p_16++; p_513++; p_8++; p_4++; p_223++; p_2++; p_135++; p_113++; p_1++;
  }
}

static void process_midi_events(
  int32_t frame,
  int32_t *event_index,
  connie_midi_event_t *in_event,
  const connie_midi_event_t *events,
  int32_t num_events
) {
  while ( *event_index < num_events && in_event->sample_offset <= (uint32_t)frame ) {
    if ( 0 == tg_midi_channel || tg_midi_channel - 1 == (in_event->data[0] & 0xF) )
      handle_midi_bytes( in_event->data, in_event->size );
    if ( ++(*event_index) < num_events )
      *in_event = events[*event_index];
  }
}

static float compute_vibrato_shift( float *shift_offset ) {
  if ( tg_vibrato == 0.0f ) {
    *shift_offset = 0.0f;
    return 0.0f;
  }
  *shift_offset += tg_vibrato * VIBRATO / (float)TG_STEP;
  if ( *shift_offset >= (float)tg_sam_in_cy )
    *shift_offset -= (float)tg_sam_in_cy;
  return tg_cycle_fl[(int)*shift_offset];
}

static void advance_sample_offsets( float shift ) {
  for ( int tone = 0; tone < 12; tone++ ) {
    tg_sample_offset[tone] += (1.0f + (float)midi_pitch / 70000.0f + 0.003f * shift * tg_vibrato * VIBRATO)
                           * tg_midi_freq[LOWNOTE + tone] / (float)TG_STEP;
    if ( tg_sample_offset[tone] >= (float)tg_sam_in_cy )
      tg_sample_offset[tone] -= (float)tg_sam_in_cy;
  }
}

void connie_dsp_process(
  float *out_l,
  float *out_r,
  int32_t num_frames,
  const connie_midi_event_t *events,
  int32_t num_events
) {
  static float shift_offset = 0.f;
  sample_t sample;
  float shift;
  static int timer = 0;

  int32_t event_index = 0;
  connie_midi_event_t in_event;
  in_event.sample_offset = 0xFFFF;
  in_event.size = 0;

  if ( num_events > 0 )
    in_event = events[0];

  for ( int32_t frame = 0; frame < num_frames; frame++ ) {
    process_midi_events( frame, &event_index, &in_event, events, num_events );
    shift = compute_vibrato_shift( &shift_offset );

    if ( ++timer > tg_sample_rate / 10000 ) {
      timer = 0;
      update_volumes();
    }

    sample = 0.0f;
    int note = LOWNOTE;
    for ( int octave = 0; octave < OCT_MIX; octave++ ) {
      for ( int tone = 0; tone < 12; tone++, note++ ) {
        int vol = tg_vol_note[note];
        sample += vol ? (sample_t)vol * getsample( (unsigned int)tone, (unsigned int)octave ) : 0.0f;
      }
    }

    advance_sample_offsets( shift );

    sample *= tg_master_vol / VOL_RAW_MAX / 16;
    sample += tg_reverb * reverb( sample );
    sample = 1.2f * clip( sample );

    out_l[frame] = sample * (1.0f - shift / 5);
    out_r[frame] = sample * (1.0f + shift / 5);
  }
}

static sample_t saw_bl( float arg, int order, int partials ) {
  while ( arg >= 2 * M_PI )
    arg -= (float)(2 * M_PI);
  sample_t result = 0.0f;
  float k = (float)(M_PI / 2 / partials);
  for ( int n = order; n <= partials; n += order ) {
    float m = cosf( (float)(n - 1) * k );
    m = m * m;
    result += sinf( (float)n * arg ) / (float)n * m;
  }
  return result;
}

static sample_t rect_bl( float arg, int order, int partials ) {
  while ( arg >= 2 * M_PI )
    arg -= (float)(2 * M_PI);
  sample_t result = 0.0f;
  float k = (float)(M_PI / 2 / partials);
  for ( int n = order; n <= partials; n += 2 * order ) {
    float m = cosf( (float)(n - 1) * k );
    m = m * m;
    result += sinf( (float)n * arg ) / (float)n * m;
  }
  return result;
}

void connie_dsp_init( int sample_rate ) {
  tg_sample_rate = sample_rate;
  inton_name = scales[intonation].label;

  float low_C = concert_pitch / 32.0f / scales[intonation].f_ratio[9];

  for ( int midinote = 0; midinote < MIDI_MAX; midinote++ ) {
    int tone = midinote % 12;
    int fmult = 1 << (midinote / 12);
    tg_midi_freq[midinote] = scales[intonation].f_ratio[tone] * low_C * (float)fmult;
    midi_vol_raw[midinote] = 0;
    tg_vol_key[midinote] = 0;
  }
  for ( int note = 0; note < NOTE_MAX; note++ )
    tg_vol_note[note] = 0;

  for ( int tone = 0; tone < 12; tone++ )
    tg_sample_offset[tone] = 0.0f;

  tg_sam_in_cy = tg_sample_rate / TG_STEP + 1;

  tg_cycle_fl = (sample_t *)malloc( (size_t)tg_sam_in_cy * sizeof( sample_t ) );
  if ( tg_cycle_fl == NULL ) {
    fprintf( stderr, "connie: memory allocation failed\n" );
    exit( 1 );
  }

  if ( CONNIE == connie_model ) {
    for ( int octave = 0; octave < OCT_SAMP; octave++ ) {
      tg_cycle_rd[octave] = (sample_t *)malloc( (size_t)tg_sam_in_cy * sizeof( sample_t ) );
      tg_cycle_sh[octave] = (sample_t *)malloc( (size_t)tg_sam_in_cy * sizeof( sample_t ) );
      if ( tg_cycle_rd[octave] == NULL || tg_cycle_sh[octave] == NULL ) {
        fprintf( stderr, "connie: memory allocation failed\n" );
        exit( 1 );
      }
    }
  }

  sample_t scale = (sample_t)(2 * M_PI / tg_sam_in_cy);
  for ( int i = 0; i < tg_sam_in_cy; i++ )
    tg_cycle_fl[i] = sinf( (float)i * scale );

  if ( CONNIE == connie_model ) {
    for ( int oct = 0; oct < OCT_SAMP; oct++ ) {
      int refnote = LOWNOTE + 12 * oct + 12;
      if ( refnote >= MIDI_MAX )
        refnote = MIDI_MAX - 1;
      int partials = (int)(tg_sample_rate / 2.0 / tg_midi_freq[refnote]);
      for ( int i = 0; i < tg_sam_in_cy; i++ ) {
        tg_cycle_rd[oct][i] = rect_bl( (float)i * scale, 1, partials );
        tg_cycle_sh[oct][i] = saw_bl( (float)i * scale, 1, partials );
      }
    }
  }

  for ( int vol = 0; vol <= VOL_RAW_MAX; vol++ ) {
    soft_step[vol] = (int)(VOL_RAW_MAX * (0.5f - 0.5f * cosf( (float)M_PI * (float)vol / (float)VOL_RAW_MAX )) + 0.5f);
    soft_step[vol + VOL_RAW_MAX] = vol + VOL_RAW_MAX;
  }
}

void connie_dsp_shutdown( void ) {
  if ( tg_cycle_fl ) {
    free( tg_cycle_fl );
    tg_cycle_fl = NULL;
  }
  for ( int octave = 0; octave < OCT_SAMP; octave++ ) {
    if ( tg_cycle_rd[octave] ) {
      free( tg_cycle_rd[octave] );
      tg_cycle_rd[octave] = NULL;
    }
    if ( tg_cycle_sh[octave] ) {
      free( tg_cycle_sh[octave] );
      tg_cycle_sh[octave] = NULL;
    }
  }
}
