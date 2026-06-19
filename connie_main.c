/*****************************************************************************
 *
 *   connie_main.c
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
 ******************************************************************************/

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <time.h>
#include <signal.h>
#ifndef _WIN32
#include <unistd.h>
#include <fpu_control.h>
#include <jack/jack.h>
#include <jack/midiport.h>
#ifdef JACK_SESSION
#include <jack/session.h>
#endif
#endif
#include <confuse.h>
#include "connie.h"
#include "connie_ui.h"
#include "reverb.h"
#include "scales.h"
#include "connie_tuning.h"

#ifdef _WIN32
extern char *optarg;
extern int optind;
extern int opterr;
extern int optopt;
int getopt( int argc, char *const argv[], const char *optstring );
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifdef _WIN32
typedef uint32_t jack_nframes_t;
typedef float jack_default_audio_sample_t;
typedef struct _jack_client jack_client_t;
typedef struct _jack_port jack_port_t;
typedef struct {
  jack_nframes_t time;
  size_t size;
  uint8_t *buffer;
} jack_midi_event_t;
typedef enum {
  JackNullOption = 0,
  JackSessionID = 0,
  JackServerFailed = 1,
  JackServerStarted = 2,
  JackNameNotUnique = 4
} jack_options_t;
typedef unsigned int jack_status_t;
#define JACK_DEFAULT_MIDI_TYPE "midi"
#define JACK_DEFAULT_AUDIO_TYPE "audio"
#define JackPortIsInput 1u
#define JackPortIsOutput 2u
#define JackPortIsPhysical 4u
jack_client_t *jack_client_open( const char *, jack_options_t, jack_status_t *, ... );
const char *jack_get_client_name( jack_client_t * );
void jack_set_error_function( void ( * )( const char * ) );
int jack_set_process_callback( jack_client_t *, int ( * )( jack_nframes_t, void * ), void * );
int jack_set_sample_rate_callback( jack_client_t *, int ( * )( jack_nframes_t, void * ), void * );
void jack_on_shutdown( jack_client_t *, void ( * )( void * ), void * );
jack_nframes_t jack_get_sample_rate( jack_client_t * );
int jack_activate( jack_client_t * );
void *jack_port_get_buffer( jack_port_t *, jack_nframes_t );
uint32_t jack_midi_get_event_count( void * );
int jack_midi_event_get( jack_midi_event_t *, void *, uint32_t );
void jack_client_close( jack_client_t * );
jack_port_t *jack_port_register( jack_client_t *, const char *, const char *, unsigned long, unsigned long );
const char **jack_get_ports( jack_client_t *, const char *, const char *, unsigned long );
const char *jack_port_name( jack_port_t * );
int jack_connect( jack_client_t *, const char *, const char * );
#endif

const char *connie_version = "0.4.3-rc6 20100928";
const char *connie_name = "long time gone";
#if defined( CONNIE_SSE )
const char *connie_cpu = "sse";
#elif defined( CONNIE_I386 )
const char *connie_cpu = "i386";
#else
const char *connie_cpu = "";
#endif

/* max leslie rotation freq (8 steps); see connie_tuning.h */
#define VIBRATO 6.4f



// ***********************************************
// tonegen
// ***********************************************


#define MIDI_MAX 128
#define OCT_SAMP (OCTAVES+2)
#define OCT_MIX (OCTAVES+3)
#define NOTE_MAX (LOWNOTE+12*OCT_MIX)
#define MAX_HARMONIC (1<<(OCT_SAMP-1))

// half tone steps
#define OCT 12
#define FIFTH 7
#define THIRD 4

// solution of sample buffers
const int TG_STEP = 8;


// one halftone step
const float tg_halftone = 1.059463094f;

// the intonation
int intonation = 0; // default

// tune the instrument
float concert_pitch = 440.0f;
int transpose = 0;
const char *inton_name;

// type of instrument
model_t connie_model = CONNIE;

// the jack name
char *jack_name = "connie";

char *uuid = NULL;
char *connie_conf = NULL;


/* Our jack client and the ports */
static jack_client_t *jack_client = NULL;
static jack_port_t *jack_midi_port;
static jack_port_t *jack_audio_port_l;
static jack_port_t *jack_audio_port_r;



typedef float sample_t;

// the current sample rate
jack_nframes_t tg_sample_rate;


// one cycle of our sound for diff voices (malloc'ed)
static sample_t *tg_cycle_fl = NULL;
static sample_t *tg_cycle_rd[ OCT_SAMP ];
static sample_t *tg_cycle_sh[ OCT_SAMP ];

// samples in cycle
static jack_nframes_t tg_sam_in_cy;

// table with frequency of each midi note
static float tg_midi_freq[MIDI_MAX];

// sample offset of each tone, advanced by rt_process
static float tg_sample_offset[12];

// actual volume of each note
static int midi_vol_raw[MIDI_MAX]; // from key press/release
static int midi_vol_smooth[MIDI_MAX]; // ramped volume
static int tg_vol_key[MIDI_MAX]; // key volume

// volume of each note after stops mixing
// maybe > MIDI_MAX!
static int tg_vol_note[NOTE_MAX];

// actual value of each midi control
int midi_cc[128];

// the midi pitch - 2000
int midi_pitch = 0;

// the actual midi prog
int midi_prog = 0;

// vibrato frequency
float tg_vibrato   = 0;
// percussion intensity
float tg_percussion = 0;
// reverb intensity
float tg_reverb = 0;

// stops
float tg_vol[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };

// voices
float tg_vol_fl  = 0;
float tg_vol_rd  = 0;
float tg_vol_sh  = 0;

// master volume
float tg_master_vol = 0.25f;

// midi channel 1..16, or 0=all
int tg_midi_channel = 0;


#define VOL_RAW_MAX 1000
static int soft_step[ 2 * VOL_RAW_MAX + 1 ];


//
void tg_panic( void ) {
  for ( int iii = 0; iii < MIDI_MAX; iii++ ) {
    midi_vol_raw[iii] = 0;
    tg_vol_key[iii] = 0;
  }
  for ( int iii = 0; iii < NOTE_MAX; iii++ )
    tg_vol_note[iii] = 0;
}



static sample_t clip( sample_t sample ) {
  if ( sample > 1.0f )
    sample = 2.0f / 3.0f;
  else if ( sample < -1.0f )
    sample = -2.0f / 3.0f;
  else
    sample = sample - ( sample * sample * sample ) / 3.0f;
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
  unsigned int pos = (unsigned int)( tg_sample_offset[tone] * (float)( 1 << octave ) );
  while ( pos >= (unsigned int)tg_sam_in_cy )
    pos -= (unsigned int)tg_sam_in_cy;

  sample_t sample = tg_cycle_fl[pos] * tg_vol_fl;

  if ( CONNIE == connie_model ) {
    if ( tg_vol_rd != 0.0f ) {
      if ( octave > 0 && tone < 4 ) {
        sample += ( (float)( 4 - tone ) * tg_cycle_rd[octave - 1][pos]
                 + (float)( 4 + tone ) * tg_cycle_rd[octave][pos] ) * tg_vol_rd / 8.0f;
      } else if ( octave < OCT_SAMP - 1 && tone > 7 ) {
        sample += ( (float)( 11 + 4 - tone ) * tg_cycle_rd[octave][pos]
                 + (float)( tone - ( 11 - 4 ) ) * tg_cycle_rd[octave + 1][pos] ) * tg_vol_rd / 8.0f;
      } else {
        sample += tg_cycle_rd[octave][pos] * tg_vol_rd;
      }
    }
    if ( tg_vol_sh != 0.0f ) {
      if ( octave > 0 && tone < 4 ) {
        sample += ( (float)( 4 - tone ) * tg_cycle_sh[octave - 1][pos]
                 + (float)( 4 + tone ) * tg_cycle_sh[octave][pos] ) * tg_vol_sh / 8.0f;
      } else if ( octave < OCT_SAMP - 1 && tone > 7 ) {
        sample += ( (float)( 11 + 4 - tone ) * tg_cycle_sh[octave][pos]
                 + (float)( tone - ( 11 - 4 ) ) * tg_cycle_sh[octave + 1][pos] ) * tg_vol_sh / 8.0f;
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
    if ( ( buf[0] >> 4 ) == 0x08 ) {
      note = transpose_note( buf[1] );
      midi_vol_raw[note] = 0;
    } else if ( ( buf[0] >> 4 ) == 0x09 ) {
      note = transpose_note( buf[1] );
      midi_vol_raw[note] = buf[2] ? VOL_RAW_MAX : 0;
    } else if ( ( buf[0] >> 4 ) == 0x0B ) {
      int cc = buf[1];
      midi_cc[cc] = buf[2];
      if ( cc == 7 )
        tg_master_vol = buf[2] * buf[2] / 127.0f / 127.0f;
      else if ( cc == 120 || cc == 123 )
        tg_panic();
    } else if ( ( buf[0] >> 4 ) == 0x0E ) {
      midi_pitch = 128 * buf[2] + buf[1] - 0x2000;
    }
  } else if ( size == 2 && ( buf[0] >> 4 ) == 0x0C ) {
    midi_prog = buf[1];
    ui_set_program( midi_prog );
  }
}

static void process_jack_midi_events(
  jack_nframes_t frame,
  jack_nframes_t *event_index,
  jack_midi_event_t *in_event,
  void *midi_buffer,
  jack_nframes_t event_count ) {
  while ( *event_index < event_count && in_event->time <= frame ) {
    if ( 0 == tg_midi_channel || tg_midi_channel - 1 == ( in_event->buffer[0] & 0xF ) )
      handle_midi_bytes( in_event->buffer, (int)in_event->size );
    if ( ++( *event_index ) < event_count )
      jack_midi_event_get( in_event, midi_buffer, *event_index );
  }
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
      *p_smooth = (int)( 2 * VOL_RAW_MAX * tg_percussion );
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
      const float *p_stop_vol = tg_vol;
      *p_16  += (int)( (float)*p_key * *p_stop_vol++ );
      *p_513 += (int)( (float)*p_key * *p_stop_vol++ );
      *p_8   += (int)( (float)*p_key * *p_stop_vol++ );
      *p_4   += (int)( (float)*p_key * *p_stop_vol++ );
      *p_223 += (int)( (float)*p_key * *p_stop_vol++ );
      *p_2   += (int)( (float)*p_key * *p_stop_vol++ );
      *p_135 += (int)( (float)*p_key * *p_stop_vol++ );
      *p_113 += (int)( (float)*p_key * *p_stop_vol++ );
      *p_1   += (int)( (float)*p_key * *p_stop_vol++ );
    }
    p_key++;
    p_16++; p_513++; p_8++; p_4++; p_223++; p_2++; p_135++; p_113++; p_1++;
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
    tg_sample_offset[tone] += ( 1.0f + (float)midi_pitch / 70000.0f + 0.003f * shift * tg_vibrato * VIBRATO )
                             * tg_midi_freq[LOWNOTE + tone] / (float)TG_STEP;
    if ( tg_sample_offset[tone] >= (float)tg_sam_in_cy )
      tg_sample_offset[tone] -= (float)tg_sam_in_cy;
  }
}

static sample_t mix_active_notes( void )
{
  sample_t sample = 0.0f;
  int note = LOWNOTE;
  for ( int octave = 0; octave < OCT_MIX; octave++ ) {
    for ( int tone = 0; tone < 12; tone++, note++ ) {
      int vol = tg_vol_note[note];
      if ( vol )
        sample += (sample_t)vol * getsample( (unsigned int)tone, (unsigned int)octave );
    }
  }
  return sample;
}

static int rt_process_cb( jack_nframes_t nframes, void *void_arg ) {
  (void)void_arg;

  static float shift_offset = 0.f;
  static int timer = 0;

  jack_nframes_t event_count = 0;
  jack_nframes_t event_index = 0;
  jack_midi_event_t in_event;
  in_event.time = 0xFFFF;

  void *midi_buffer = jack_port_get_buffer( jack_midi_port, nframes );
  event_count = jack_midi_get_event_count( midi_buffer );
  if ( event_count > 0 )
    jack_midi_event_get( &in_event, midi_buffer, 0 );

  sample_t *out_l = (sample_t *)jack_port_get_buffer( jack_audio_port_l, nframes );
  sample_t *out_r = (sample_t *)jack_port_get_buffer( jack_audio_port_r, nframes );

  for ( jack_nframes_t frame = 0; frame < nframes; frame++ ) {
    process_jack_midi_events( frame, &event_index, &in_event, midi_buffer, event_count );

    float shift = compute_vibrato_shift( &shift_offset );

    if ( ++timer > (int)( tg_sample_rate / 10000 ) ) {
      timer = 0;
      update_volumes();
    }

    sample_t sample = mix_active_notes();

    advance_sample_offsets( shift );

    sample *= tg_master_vol / (float)VOL_RAW_MAX / 16.0f;
    sample += tg_reverb * reverb( sample );
    sample = 1.2f * clip( sample );

    out_l[frame] = sample * ( 1.0f - shift / 5.0f );
    out_r[frame] = sample * ( 1.0f + shift / 5.0f );
  }

  return 0;
}


// callback if sample rate changes
static int jack_srate_cb( jack_nframes_t nframes, void *arg ) {
  (void)arg;
  printf( "connie: JACK sample rate is now %lu/sec\n", (unsigned long)nframes );
  tg_sample_rate = nframes;
  return 0;
}



// callback in case of error
static void jack_error_cb( const char *desc ) {
  fprintf( stderr, "connie: JACK error (%s)\n", desc );
  jack_client = NULL;
  exit( 1 );
}



// callback at jack shutdown
static void jack_shutdown_cb( void *arg ) {
  (void)arg;
  fprintf( stderr, "connie: JACK shutdown\n" );
  exit( 0 );
}


#ifdef JACK_SESSION
void
session_callback (jack_session_event_t *event, void *arg)
{
  char retval[256];
  printf ("session notification\n");
  printf ("path %s, uuid %s, type: %d\n",
           event->session_dir, event->client_uuid, event->type );


  snprintf (retval, sizeof(retval), "x-terminal-emulator -e \"/tmp/connie -U%s.connie\"", event->session_dir);
  event->command_line = strdup (retval);

  jack_session_reply( jack_client, event );

  ui_save( event->type, event->session_dir );

  jack_session_event_free (event);
}
#endif



// called via atexit()
static void connie_tg_shutdown( void )
{
  if ( jack_client ) {
    jack_client_close( jack_client );
    jack_client = NULL;
  }
  if ( tg_cycle_fl )
    free( tg_cycle_fl );
  tg_cycle_fl = NULL;
  for ( int octave = 0; octave < OCT_SAMP; octave++ ) {
    if ( tg_cycle_rd[octave] )
      free( tg_cycle_rd[octave] );
    tg_cycle_rd[octave] = NULL;
    if ( tg_cycle_sh[octave] )
      free( tg_cycle_sh[octave] );
    tg_cycle_sh[octave] = NULL;
  }
}



// The signal handler function to catch ^C, xterm close etc.
static void ctrl_c_handler( int sig) {
  fprintf( stderr, "Signal %d received - aborting...", sig );
  fflush( stderr );
  exit( 0 ); // -> atexit( connie_tg_shutdown )
}  // ctrl_c_handler()





// bandlimited sawtooth and rectangle
// Gibbs smoothing according:
// Joe Wright: Synthesising bandlimited waveforms using wavetables
// www.musicdsp.org/files/bandlimited.pdf
//
static sample_t saw_bl( float arg, int order, int partials ) {
  while ( arg >= (float)( 2 * M_PI ) )
    arg -= (float)( 2 * M_PI );
  sample_t result = 0.0f;
  float k = (float)( M_PI / 2 / partials );
  for ( int n = order; n <= partials; n += order ) {
    float m = cosf( (float)( n - 1 ) * k );
    m = m * m;
    result += sinf( (float)n * arg ) / (float)n * m;
  }
  return result;
}


static sample_t rect_bl( float arg, int order, int partials ) {
  while ( arg >= (float)( 2 * M_PI ) )
    arg -= (float)( 2 * M_PI );
  sample_t result = 0.0f;
  float k = (float)( M_PI / 2 / partials );
  for ( int n = order; n <= partials; n += 2 * order ) {
    float m = cosf( (float)( n - 1 ) * k );
    m = m * m;
    result += sinf( (float)n * arg ) / (float)n * m;
  }
  return result;
}


static void tg_init( int sample_rate )
{
  float low_C = concert_pitch / 32.0f / scales[intonation].f_ratio[9];

  for ( int midinote = 0; midinote < MIDI_MAX; midinote++ ) {
    int tone = midinote % 12;
    int fmult = 1 << ( midinote / 12 );
    tg_midi_freq[midinote] = scales[intonation].f_ratio[tone] * low_C * (float)fmult;
    midi_vol_raw[midinote] = 0;
    tg_vol_key[midinote] = 0;
  }
  for ( int note = 0; note < NOTE_MAX; note++ )
    tg_vol_note[note] = 0;

  for ( int tone = 0; tone < 12; tone++ )
    tg_sample_offset[tone] = 0.0f;

  tg_sam_in_cy = sample_rate / TG_STEP + 1;

  tg_cycle_fl = (sample_t *)malloc( (size_t)tg_sam_in_cy * sizeof( sample_t ) );
  if ( tg_cycle_fl == NULL ) {
    fprintf( stderr, "memory allocation failed\n" );
    exit( 1 );
  }

  if ( CONNIE == connie_model ) {
    for ( int octave = 0; octave < OCT_SAMP; octave++ ) {
      tg_cycle_rd[octave] = (sample_t *)malloc( (size_t)tg_sam_in_cy * sizeof( sample_t ) );
      tg_cycle_sh[octave] = (sample_t *)malloc( (size_t)tg_sam_in_cy * sizeof( sample_t ) );
      if ( tg_cycle_rd[octave] == NULL || tg_cycle_sh[octave] == NULL ) {
        fprintf( stderr, "memory allocation failed\n" );
        exit( 1 );
      }
    }
  }

  sample_t scale = (sample_t)( 2 * M_PI / tg_sam_in_cy );
  printf( "Preparing the voices" );
  for ( int i = 0; i < tg_sam_in_cy; i++ )
    tg_cycle_fl[i] = sinf( (float)i * scale );

  if ( CONNIE == connie_model ) {
    for ( int oct = 0; oct < OCT_SAMP; oct++ ) {
      int refnote = LOWNOTE + 12 * oct + 12;
      if ( refnote >= MIDI_MAX )
        refnote = MIDI_MAX - 1;
      int partials = (int)( sample_rate / 2.0 / tg_midi_freq[refnote] );
      printf( "." );
      fflush( stdout );
      for ( int i = 0; i < tg_sam_in_cy; i++ ) {
        tg_cycle_rd[oct][i] = rect_bl( (float)i * scale, 1, partials );
        tg_cycle_sh[oct][i] = saw_bl( (float)i * scale, 1, partials );
      }
    }
  }

  for ( int vol = 0; vol <= VOL_RAW_MAX; vol++ ) {
    soft_step[vol] = (int)( VOL_RAW_MAX * ( 0.5f - 0.5f * cosf( (float)M_PI * (float)vol / (float)VOL_RAW_MAX ) ) + 0.5f );
    soft_step[vol + VOL_RAW_MAX] = vol + VOL_RAW_MAX;
  }
  puts( "" );
}


static char *connie_strdup( const char *s )
{
#ifdef _MSC_VER
  return _strdup( s );
#else
  return strdup( s );
#endif
}

typedef struct {
  int autoconnect;
  char *midi_port;
  int printhelp;
  keybd_t keybd;
  int drawbars[20];
} cli_state_t;

static void print_usage( void )
{
  printf( "usage: connie [opts]\n" );
  printf( "  -a\t\t\tautoconnect to system:playback ports\n" );
  printf( "  -c CHANNEL\t\tMIDI channel (1..16), 0=all (default)\n" );
  printf( "  -f\t\t\tfrench AZERTY keyboard\n" );
  printf( "  -g\t\t\tgerman QWERTZ keyboard\n" );
  printf( "  -h\t\t\tthis help msg\n" );
  printf( "  -i INSTRUMENT\t\t0: connie (default), 1: poor-man's-hammond\n" );
  printf( "  -m MIDI_PORT\t\tconnect with midi port\n" );
  printf( "  -p PITCH\t\tconcert pitch 220..880 Hz\n" );
  printf( "  -s INTONATION_SCALE\t 0: %s\n", scales[0].label );
  for ( int iii = 1; iii < NSCALES; iii++ )
    printf( "\t\t\t%2d: %s\n", iii, scales[iii].label );
  printf( "  -t TRANSPOSE\t\ttranspose -12..+12 semitones\n" );
  printf( "  -v\t\t\tprint version\n" );
  printf( "  -C configfile\t\tload config file\n" );
  printf( "  -U UUID\t\tset jack session UUID\n" );
}

static void load_config_file( const char *path, cli_state_t *cli )
{
  cfg_opt_t opts[] = {
    CFG_STR( "UUID", NULL, CFGF_NONE ),
    CFG_STR( "jack_name", "connie", CFGF_NONE ),
    CFG_INT( "connie_model", 0, CFGF_NONE ),
    CFG_INT( "keybd", 0, CFGF_NONE ),
    CFG_INT( "intonation", 0, CFGF_NONE ),
    CFG_FLOAT( "concert_pitch", 440.0, CFGF_NONE ),
    CFG_INT( "transpose", 0, CFGF_NONE ),
    CFG_INT( "midi_channel", 0, CFGF_NONE ),
    CFG_INT_LIST( "drawbars", 0, CFGF_NONE ),
    CFG_END()
  };
  cfg_t *cfg = cfg_init( opts, CFGF_NONE );
  if ( cfg_parse( cfg, path ) == CFG_PARSE_ERROR )
    exit( 1 );

  if ( !uuid && cfg_getstr( cfg, "UUID" ) )
    uuid = connie_strdup( cfg_getstr( cfg, "UUID" ) );
  jack_name = connie_strdup( cfg_getstr( cfg, "jack_name" ) );
  connie_model = cfg_getint( cfg, "connie_model" );
  cli->keybd = cfg_getint( cfg, "keybd" );
  intonation = cfg_getint( cfg, "intonation" );
  concert_pitch = cfg_getfloat( cfg, "concert_pitch" );
  transpose = cfg_getint( cfg, "transpose" );
  tg_midi_channel = cfg_getint( cfg, "midi_channel" );
  cli->drawbars[0] = cfg_size( cfg, "drawbars" );
  for ( int iii = 0; iii < cli->drawbars[0]; iii++ )
    cli->drawbars[iii + 1] = cfg_getnint( cfg, "drawbars", iii );
  cfg_free( cfg );
}

static void handle_cli_option( int c, cli_state_t *cli )
{
  switch ( c ) {
    case 'a':
      cli->autoconnect = 1;
      printf( "autoconnect\n" );
      break;
    case 'c':
      tg_midi_channel = atoi( optarg );
      if ( tg_midi_channel < 0 || tg_midi_channel > 16 )
        tg_midi_channel = 0;
      printf( "midi channel %d\n", tg_midi_channel );
      break;
    case 'f':
      cli->keybd = AZERTY;
      printf( "french AZERTY kbd\n" );
      break;
    case 'g':
      cli->keybd = QWERTZ;
      printf( "german QWERTZ kbd\n" );
      break;
    case 'h':
      cli->printhelp = 1;
      break;
    case 'i':
      connie_model = atoi( optarg );
      if ( connie_model < 0 || connie_model > HAMMOND )
        connie_model = CONNIE;
      printf( "instrument: %d\n", connie_model );
      break;
    case 'm':
      cli->midi_port = optarg;
      printf( "MIDI port: %s\n", cli->midi_port );
      break;
    case 'n':
      jack_name = optarg;
      printf( "jack_name: %s\n", jack_name );
      break;
    case 'p':
      concert_pitch = (float)atof( optarg );
      if ( concert_pitch < 220.0f || concert_pitch > 880.0f )
        concert_pitch = 440.0f;
      printf( "concert pitch = %5.1f Hz\n", concert_pitch );
      break;
    case 's':
      intonation = atoi( optarg );
      if ( intonation < 0 || intonation >= NSCALES )
        intonation = 0;
      inton_name = scales[intonation].label;
      printf( "%s\n", inton_name );
      break;
    case 't':
      transpose = atoi( optarg );
      if ( transpose < -12 || transpose > 12 )
        transpose = 0;
      printf( "transpose %d semitones\n", transpose );
      break;
    case 'v':
      printf( "%s_%s %s (%s)\n", jack_name, connie_cpu, connie_version, connie_name );
      exit( 1 );
    case 'C':
      connie_conf = optarg;
      load_config_file( connie_conf, cli );
      break;
    case 'U':
      uuid = optarg;
      break;
    case '?':
      if ( 'c' == optopt || 'i' == optopt || 'm' == optopt || 'n' == optopt
        || 'p' == optopt || 's' == optopt || 't' == optopt
        || 'C' == optopt || 'U' == optopt )
        fprintf( stderr, "Option `-%c' requires an argument.\n", optopt );
      else if ( isprint( optopt ) )
        fprintf( stderr, "Unknown option `-%c'.\n", optopt );
      else
        fprintf( stderr, "Unknown option character `\\x%x'.\n", optopt );
      /* fall through */
    default:
      cli->printhelp = 1;
      break;
  }
}

static void parse_command_line( int argc, char **argv, cli_state_t *cli )
{
  opterr = 0;
  int c;
  while ( ( c = getopt( argc, argv, "ac:fghi:m:n:p:s:t:vC:U:" ) ) != -1 )
    handle_cli_option( c, cli );
  inton_name = scales[intonation].label;
}

static void setup_fpu( void )
{
#ifndef _WIN32
  fpu_control_t cw;
  _FPU_GETCW( cw );
  cw |= _FPU_RC_ZERO;
  _FPU_SETCW( cw );
#endif
}

static void open_jack_client( void )
{
  jack_status_t status;

#ifdef JACK_SESSION
  if ( !uuid )
    jack_client = jack_client_open( jack_name, JackNullOption, &status );
  else {
    printf( "UUID %s\n", uuid );
    jack_client = jack_client_open( jack_name, JackSessionID, &status, uuid );
  }
#else
  jack_client = jack_client_open( jack_name, JackNullOption, &status );
#endif

  if ( jack_client == NULL ) {
    fprintf( stderr, "jack_client_open() failed, status = 0x%2.0x\n", status );
    if ( status & JackServerFailed )
      fprintf( stderr, "Unable to connect to JACK server\n" );
    exit( 1 );
  }
  if ( status & JackServerStarted )
    fprintf( stderr, "JACK server started\n" );
  if ( status & JackNameNotUnique ) {
    jack_name = jack_get_client_name( jack_client );
    fprintf( stderr, "unique name `%s' assigned\n", jack_name );
  }
}

static void register_jack_callbacks( void )
{
  jack_set_error_function( jack_error_cb );
  jack_set_process_callback( jack_client, rt_process_cb, 0 );
  jack_set_sample_rate_callback( jack_client, jack_srate_cb, 0 );
  jack_on_shutdown( jack_client, jack_shutdown_cb, 0 );
#ifdef JACK_SESSION
  jack_set_session_callback( jack_client, session_callback, NULL );
#endif
}

static int try_autoconnect_pair( const char *port_l, const char *port_r )
{
  if ( jack_connect( jack_client, jack_port_name( jack_audio_port_l ), port_l ) != 0 )
    return 0;
  if ( !port_r )
    return 1;
  return jack_connect( jack_client, jack_port_name( jack_audio_port_r ), port_r ) == 0;
}

static void autoconnect_playback( void )
{
  const char **jack_ports = jack_get_ports(
    jack_client, NULL, NULL, JackPortIsPhysical | JackPortIsInput );
  if ( !jack_ports ) {
    fprintf( stderr, "connie: cannot find any physical playback ports\n" );
    exit( 1 );
  }
  for ( const char **pp = jack_ports; *pp; pp += 2 ) {
    if ( try_autoconnect_pair( pp[0], pp[1] ) )
      break;
  }
  free( jack_ports );
}

static void connect_midi_port( const char *midi_port )
{
  if ( jack_connect( jack_client, midi_port, jack_port_name( jack_midi_port ) ) ) {
    fprintf( stderr, "connie: cannot connect %s - %s\n", midi_port, jack_port_name( jack_midi_port ) );
    exit( 1 );
  }
}

static void start_jack_and_ui( const cli_state_t *cli )
{
  register_jack_callbacks();

  tg_sample_rate = jack_get_sample_rate( jack_client );
  printf( "sample rate: %lu/sec\n", (unsigned long)tg_sample_rate );
  tg_init( tg_sample_rate );

  jack_midi_port = jack_port_register( jack_client, "midi_in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0 );
  jack_audio_port_l = jack_port_register( jack_client, "left", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0 );
  jack_audio_port_r = jack_port_register( jack_client, "right", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0 );

  if ( jack_activate( jack_client ) ) {
    fprintf( stderr, "cannot activate client\n" );
    exit( 1 );
  }

  atexit( connie_tg_shutdown );

  if ( cli->autoconnect )
    autoconnect_playback();
  if ( cli->midi_port )
    connect_midi_port( cli->midi_port );

  ui_init( connie_model, cli->keybd );
  if ( cli->drawbars[0] )
    ui_set_drawbars( cli->drawbars );
  ui_loop( connie_name );
}


int main( int argc, char *argv[] ) {
  cli_state_t cli = { 0, NULL, 0, QWERTY, { 0 } };

  setup_fpu();

  signal( SIGHUP, ctrl_c_handler );
  signal( SIGINT, ctrl_c_handler );
  signal( SIGQUIT, ctrl_c_handler );
  signal( SIGABRT, ctrl_c_handler );
  signal( SIGTERM, ctrl_c_handler );

  parse_command_line( argc, argv, &cli );

  if ( cli.printhelp ) {
    print_usage();
    exit( 1 );
  }

  open_jack_client();
  start_jack_and_ui( &cli );

  exit( 0 );
}

