/*****************************************************************************
 *
 *   connie_lv2.c
 *
 *   Connie Vox Continental LV2 plugin
 *
 *****************************************************************************/

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/util.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>

#include "connie.h"
#include "connie_dsp.h"
#include "connie_params.h"
#include "connie_tg.h"

#define CONNIE_LV2_URI "https://ho-ro.net/connie/lv2"

#define PORT_OUT_L    0
#define PORT_OUT_R    1
#define PORT_MIDI     2
#define PORT_ENABLED  3
#define PORT_DB_16    4
#define PORT_DB_COUNT 10
#define PORT_MASTER   14
#define PORT_TRANSPOSE 15
#define PORT_PRESET   16
#define PORT_COUNT    17

typedef struct {
  float *out_l;
  float *out_r;
  const LV2_Atom_Sequence *midi;
  float *enabled;
  float *drawbars[PORT_DB_COUNT];
  float *master;
  float *transpose;
  float *preset;
  LV2_URID midi_event_id;
  double sample_rate;
  int host_drawbars_set;
} ConnieLV2;

static int norm_to_drawbar( float v ) {
  int d = (int)( v * 8.0f + 0.5f );
  if ( d < 0 )
    d = 0;
  if ( d > 8 )
    d = 8;
  return d;
}

static void handle_atom_midi( const ConnieLV2 *h, const LV2_Atom_Event *event ) {
  const uint8_t *data = (const uint8_t *)LV2_ATOM_BODY_CONST( &event->body ); /* NOSONAR lv2 atom payload */
  uint32_t size = event->body.size;

  if ( event->body.type != h->midi_event_id || !data || size < 1 )
    return;

  /* Some hosts wrap raw MIDI in a nested MidiEvent atom. */
  if ( size >= sizeof( LV2_Atom ) ) {
    const LV2_Atom *atom = (const LV2_Atom *)data;
    if ( atom->type == h->midi_event_id && atom->size + sizeof( LV2_Atom ) <= size ) {
      data = (const uint8_t *)( atom + 1 ); /* NOSONAR payload follows LV2_Atom header */
      size = atom->size;
    }
  }

  uint8_t buf[3];
  int32_t n = size > 3 ? 3 : (int32_t)size;
  memcpy( buf, data, (size_t)n ); /* NOSONAR n = min(size, 3), size validated above */
  connie_dsp_midi( buf, n );
}

static void apply_controls( ConnieLV2 *h ) {
  float drawbar_sum = 0.0f;
  for ( int i = 0; i < PORT_DB_COUNT; i++ )
    drawbar_sum += *h->drawbars[i];

  if ( !h->host_drawbars_set && drawbar_sum < 0.001f ) {
    /* Headless hosts (e.g. mod-host) often leave controls at 0; keep preset. */
  } else {
    h->host_drawbars_set = 1;
    for ( int i = 0; i < PORT_DB_COUNT; i++ )
      connie_params_set_drawbar( i, norm_to_drawbar( *h->drawbars[i] ) );
  }
  connie_params_apply_volumes();

  if ( h->master ) {
    float m = *h->master;
    tg_master_vol = ( m > 0.0f ) ? m : 0.25f;
  }

  if ( h->transpose )
    transpose = (int)( *h->transpose * 24.0f + 0.5f ) - 12;

  if ( h->preset ) {
    int prog = (int)( *h->preset * 9.0f + 0.5f );
    if ( prog < 0 )
      prog = 0;
    if ( prog > 9 )
      prog = 9;
    if ( prog != connie_params_get_program() )
      connie_params_set_program( prog );
  }
}

static LV2_Handle instantiate( const LV2_Descriptor *descriptor,
                               double sample_rate,
                               const char *bundle_path,
                               const LV2_Feature *const *features ) {
  (void)descriptor;
  (void)bundle_path;

  ConnieLV2 *h = (ConnieLV2 *)calloc( 1, sizeof( *h ) );
  if ( !h )
    return NULL;

  h->sample_rate = sample_rate;
  h->midi_event_id = 0;

  LV2_URID_Map *map = NULL;
  for ( int i = 0; features && features[i]; i++ ) {
    if ( !strcmp( features[i]->URI, LV2_URID__map ) )
      map = (LV2_URID_Map *)features[i]->data;
  }
  if ( !map ) {
    free( h );
    return NULL;
  }
  h->midi_event_id = map->map( map->handle, "http://lv2plug.in/ns/ext/midi#MidiEvent" );

  return h;
}

static void connect_port( LV2_Handle instance, uint32_t port, void *data ) {
  ConnieLV2 *h = (ConnieLV2 *)instance;

  switch ( port ) {
    case PORT_OUT_L:    h->out_l = (float *)data; break;
    case PORT_OUT_R:    h->out_r = (float *)data; break;
    case PORT_MIDI:     h->midi = (const LV2_Atom_Sequence *)data; break;
    case PORT_ENABLED:  h->enabled = (float *)data; break;
    case PORT_MASTER:   h->master = (float *)data; break;
    case PORT_TRANSPOSE: h->transpose = (float *)data; break;
    case PORT_PRESET:   h->preset = (float *)data; break;
    default:
      if ( port >= PORT_DB_16 && port < PORT_DB_16 + PORT_DB_COUNT )
        h->drawbars[port - PORT_DB_16] = (float *)data;
      break;
  }
}

static void activate( LV2_Handle instance ) {
  ConnieLV2 *h = (ConnieLV2 *)instance;
  connie_params_init( CONNIE );
  connie_dsp_init( (int)h->sample_rate );
  apply_controls( h );
}

static void deactivate( LV2_Handle instance ) {
  (void)instance;
  connie_dsp_panic();
  connie_dsp_shutdown();
}

static void run( LV2_Handle instance, uint32_t nframes ) {
  ConnieLV2 *h = (ConnieLV2 *)instance;

  if ( !h->out_l || !h->out_r )
    return;

  if ( h->enabled && *h->enabled <= 0.0f ) {
    memset( h->out_l, 0, nframes * sizeof( float ) );
    memset( h->out_r, 0, nframes * sizeof( float ) );
    return;
  }

  apply_controls( h );

  uint32_t frame = 0;
  float *out_l = h->out_l;
  float *out_r = h->out_r;

  if ( h->midi ) {
    LV2_ATOM_SEQUENCE_FOREACH( h->midi, ev ) {
      if ( ev->body.type != h->midi_event_id )
        continue;

      const LV2_Atom_Event *event = (const LV2_Atom_Event *)ev;
      uint32_t size = event->body.size;

      uint32_t ev_frame = event->time.frames;
      if ( ev_frame > nframes )
        ev_frame = nframes;
      if ( ev_frame > frame ) {
        uint32_t chunk = ev_frame - frame;
        connie_dsp_process( out_l, out_r, (int32_t)chunk, NULL, 0 );
        out_l += chunk;
        out_r += chunk;
        frame = ev_frame;
      }

      if ( size >= 1 ) {
        handle_atom_midi( h, event );
      }
    }
  }

  if ( frame < nframes )
    connie_dsp_process( out_l, out_r, (int32_t)( nframes - frame ), NULL, 0 );
}

static void cleanup( LV2_Handle instance ) {
  free( instance );
}

static const LV2_Descriptor connie_descriptor = {
  CONNIE_LV2_URI,
  instantiate,
  connect_port,
  activate,
  run,
  deactivate,
  cleanup
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor *lv2_descriptor( uint32_t index ) {
  return index == 0 ? &connie_descriptor : NULL;
}
