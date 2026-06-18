/*****************************************************************************
 *
 *   headless_lv2_test.c
 *
 *   Minimal LV2 host smoke test: atom MIDI in, audio out
 *
 *****************************************************************************/

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/util.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>

extern const LV2_Descriptor *lv2_descriptor( uint32_t index );

#define MIDI_EVENT_URI "http://lv2plug.in/ns/ext/midi#MidiEvent"

#define LV2_ATOM_HEADER_BYTES          8u
#define CONNIE_LV2_EVENT_BODY_OFFSET   8u

#define URID_ATOM_SEQUENCE 2u
#define URID_MIDI_EVENT    3u

typedef struct {
  const char *uri;
  LV2_URID    id;
} urid_entry_t;

static const urid_entry_t urid_table[] = {
  { LV2_ATOM__Sequence, URID_ATOM_SEQUENCE },
  { MIDI_EVENT_URI,     URID_MIDI_EVENT },
};

static LV2_URID map_uri( LV2_URID_Map_Handle handle, const char *uri ) {
  (void)handle;
  for ( size_t i = 0; i < sizeof( urid_table ) / sizeof( urid_table[0] ); i++ ) {
    if ( !strcmp( uri, urid_table[i].uri ) )
      return urid_table[i].id;
  }
  return 0;
}

static float buffer_energy( const float *buf, uint32_t n ) {
  float acc = 0.0f;
  for ( uint32_t i = 0; i < n; i++ ) {
    float v = buf[i];
    acc += v < 0.0f ? -v : v;
  }
  return acc;
}

static void init_sequence( LV2_Atom_Sequence *seq, LV2_URID seq_urid ) {
  seq->atom.type = seq_urid;
  seq->atom.size = sizeof( LV2_Atom_Sequence_Body );
}

static void write_u32_le( uint8_t *p, uint32_t v ) {
  p[0] = (uint8_t)( v & 0xffu );
  p[1] = (uint8_t)( ( v >> 8 ) & 0xffu );
  p[2] = (uint8_t)( ( v >> 16 ) & 0xffu );
  p[3] = (uint8_t)( ( v >> 24 ) & 0xffu );
}

static void write_i64_le( uint8_t *p, int64_t v ) {
  for ( int i = 0; i < 8; i++ )
    p[i] = (uint8_t)( (uint64_t)v >> ( 8 * i ) );
}

static int append_note_on( LV2_Atom_Sequence *seq,
                           uint32_t            capacity,
                           LV2_URID            midi_urid,
                           uint32_t            frame,
                           uint8_t             note,
                           uint8_t             velocity ) {
  uint8_t storage[64];
  const size_t body_off     = CONNIE_LV2_EVENT_BODY_OFFSET;
  const size_t payload_off  = body_off + sizeof( LV2_Atom );

  memset( storage, 0, sizeof( storage ) );
  write_i64_le( storage, (int64_t)frame );
  write_u32_le( storage + body_off, midi_urid );
  write_u32_le( storage + body_off + 4, 3u );
  storage[payload_off + 0] = 0x90;
  storage[payload_off + 1] = note;
  storage[payload_off + 2] = velocity;

  return lv2_atom_sequence_append_event( seq, capacity, (const LV2_Atom_Event *)storage ) ? 0 : -1;
}

static float render_energy( const LV2_Descriptor *desc,
                            LV2_Handle            handle,
                            LV2_Atom_Sequence    *midi,
                            uint32_t              nframes ) {
  float *left  = (float *)calloc( nframes, sizeof( float ) );
  float *right = (float *)calloc( nframes, sizeof( float ) );
  if ( !left || !right ) {
    free( left );
    free( right );
    return 0.0f;
  }

  desc->connect_port( handle, 0, left );
  desc->connect_port( handle, 1, right );
  desc->connect_port( handle, 2, midi );
  desc->run( handle, nframes );

  float energy = buffer_energy( left, nframes ) + buffer_energy( right, nframes );
  free( left );
  free( right );
  return energy;
}

int main( void ) {
  const LV2_Descriptor *desc = lv2_descriptor( 0 );
  if ( !desc || !desc->instantiate || !desc->connect_port || !desc->activate ||
       !desc->run || !desc->deactivate || !desc->cleanup ) {
    fprintf( stderr, "FAIL: missing LV2 descriptor\n" );
    return EXIT_FAILURE;
  }

  LV2_URID_Map map = { NULL, map_uri };
  LV2_Feature   urid_feature = { LV2_URID__map, &map };
  const LV2_Feature *features[] = { &urid_feature, NULL };

  LV2_Handle handle = desc->instantiate( desc, 48000.0, "", features );
  if ( !handle ) {
    fprintf( stderr, "FAIL: instantiate returned NULL\n" );
    return EXIT_FAILURE;
  }

  float enabled   = 1.0f;
  float master    = 1.0f;
  float transpose = 0.5f;
  float preset    = 0.0f;
  float drawbars[10];

  memset( drawbars, 0, sizeof( drawbars ) );

  desc->connect_port( handle, 3, &enabled );
  desc->connect_port( handle, 14, &master );
  desc->connect_port( handle, 15, &transpose );
  desc->connect_port( handle, 16, &preset );
  for ( int i = 0; i < 10; i++ )
    desc->connect_port( handle, (uint32_t)( 4 + i ), &drawbars[i] );

  desc->activate( handle );

  uint8_t midi_buf[512];
  LV2_Atom_Sequence *midi = (LV2_Atom_Sequence *)midi_buf;
  init_sequence( midi, URID_ATOM_SEQUENCE );

  float silent = render_energy( desc, handle, midi, 1024 );
  if ( silent > 1e-3f ) {
    fprintf( stderr, "FAIL: expected silence with no MIDI, got energy=%g\n", silent );
    desc->deactivate( handle );
    desc->cleanup( handle );
    return EXIT_FAILURE;
  }

  if ( append_note_on( midi, (uint32_t)sizeof( midi_buf ), URID_MIDI_EVENT, 0, 60, 127 ) ) {
    fprintf( stderr, "FAIL: could not append MIDI note-on event\n" );
    desc->deactivate( handle );
    desc->cleanup( handle );
    return EXIT_FAILURE;
  }

  float on_energy = render_energy( desc, handle, midi, 4096 );
  if ( on_energy <= 1e-4f ) {
    fprintf( stderr, "FAIL: expected audio for atom MIDI note on, got %g\n", on_energy );
    desc->deactivate( handle );
    desc->cleanup( handle );
    return EXIT_FAILURE;
  }

  master = 0.2f;
  float low = render_energy( desc, handle, midi, 4096 );
  master = 1.0f;
  float high = render_energy( desc, handle, midi, 4096 );
  if ( high <= low * 1.5f ) {
    fprintf( stderr, "FAIL: master volume ineffective via LV2 (low=%g high=%g)\n", low, high );
    desc->deactivate( handle );
    desc->cleanup( handle );
    return EXIT_FAILURE;
  }

  desc->deactivate( handle );
  desc->cleanup( handle );

  printf( "headless-lv2-test: PASS\n" );
  return EXIT_SUCCESS;
}
