//------------------------------------------------------------------------
// Connie Vox Continental VST3 instrument (no custom editor)
//------------------------------------------------------------------------

#include "connie_cids.h"
#include "connie_paramids.h"
#include "version.h"

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstcomponent.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include "base/source/fstreamer.h"

extern "C" {
#include "connie_dsp.h"
#include "connie_params.h"
#include "connie.h"
#include "connie_tg.h"
}

#include <cmath>
#include <cstring>
#include <vector>

namespace Steinberg {
namespace Vst {

namespace {

constexpr int kDrawbarSteps = 8;
constexpr int kPresetSteps = 9;
constexpr int kTransposeSteps = 24;

int drawbarToInt( ParamValue v ) {
  int val = static_cast<int>( std::lround( v * kDrawbarSteps ) );
  if ( val < 0 )
    val = 0;
  if ( val > kDrawbarSteps )
    val = kDrawbarSteps;
  return val;
}

void applyDrawbarParam( ParamID id, ParamValue value ) {
  int drawbar = drawbarToInt( value );
  switch ( id ) {
    case kParam16:    connie_params_set_drawbar( 0, drawbar ); break;
    case kParam8:     connie_params_set_drawbar( 1, drawbar ); break;
    case kParam4:     connie_params_set_drawbar( 2, drawbar ); break;
    case kParamIV:    connie_params_set_drawbar( 3, drawbar ); break;
    case kParamFlute: connie_params_set_drawbar( 4, drawbar ); break;
    case kParamReed:  connie_params_set_drawbar( 5, drawbar ); break;
    case kParamSharp: connie_params_set_drawbar( 6, drawbar ); break;
    case kParamPerc:  connie_params_set_drawbar( 7, drawbar ); break;
    case kParamVibrato: connie_params_set_drawbar( 8, drawbar ); break;
    case kParamReverb:  connie_params_set_drawbar( 9, drawbar ); break;
    default: break;
  }
}

void addDrawbarParam( ParameterContainer &params, const char *title, ParamID id, int defaultVal ) {
  ParamValue norm = static_cast<ParamValue>( defaultVal ) / kDrawbarSteps;
  params.addParameter( UString128( title ), nullptr, kDrawbarSteps, norm,
                       ParameterInfo::kCanAutomate, id );
}

} // namespace

//------------------------------------------------------------------------
class ConniePlugin : public SingleComponentEffect {
public:
  ConniePlugin() = default;

  static FUnknown *createInstance( void * ) {
    return static_cast<IAudioProcessor *>( new ConniePlugin );
  }

  tresult PLUGIN_API initialize( FUnknown *context ) SMTG_OVERRIDE {
    tresult result = SingleComponentEffect::initialize( context );
    if ( result != kResultOk )
      return result;

    addAudioOutput( STR16( "Output" ), SpeakerArr::kStereo );
    addEventInput( STR16( "Event In" ), 1 );

    connie_params_init( CONNIE );

    addDrawbarParam( parameters, "16'", kParam16, 6 );
    addDrawbarParam( parameters, "8'", kParam8, 8 );
    addDrawbarParam( parameters, "4'", kParam4, 6 );
    addDrawbarParam( parameters, "Mixture IV", kParamIV, 8 );
    addDrawbarParam( parameters, "Flute", kParamFlute, 8 );
    addDrawbarParam( parameters, "Reed", kParamReed, 4 );
    addDrawbarParam( parameters, "Sharp", kParamSharp, 0 );
    addDrawbarParam( parameters, "Percussion", kParamPerc, 0 );
    addDrawbarParam( parameters, "Vibrato", kParamVibrato, 0 );
    addDrawbarParam( parameters, "Reverb", kParamReverb, 4 );

    parameters.addParameter( STR16( "Master" ), STR16( "%" ), 0, 0.25,
                             ParameterInfo::kCanAutomate, kParamMaster );
    parameters.addParameter( STR16( "Transpose" ), STR16( "st" ), kTransposeSteps,
                             static_cast<ParamValue>( 12 ) / kTransposeSteps,
                             ParameterInfo::kCanAutomate, kParamTranspose );
    parameters.addParameter( STR16( "Preset" ), nullptr, kPresetSteps, 0,
                             ParameterInfo::kCanAutomate, kParamPreset );

    syncParamsFromEngine();
    return kResultOk;
  }

  tresult PLUGIN_API terminate() SMTG_OVERRIDE {
    connie_dsp_shutdown();
    return SingleComponentEffect::terminate();
  }

  tresult PLUGIN_API setActive( TBool state ) SMTG_OVERRIDE {
    if ( state ) {
      connie_dsp_init( sampleRate );
      applyAllParams();
    } else {
      connie_dsp_shutdown();
      connie_dsp_panic();
    }
    return kResultOk;
  }

  tresult PLUGIN_API setupProcessing( ProcessSetup &newSetup ) SMTG_OVERRIDE {
    sampleRate = static_cast<int>( newSetup.sampleRate );
    return SingleComponentEffect::setupProcessing( newSetup );
  }

  tresult PLUGIN_API canProcessSampleSize( int32 symbolicSampleSize ) SMTG_OVERRIDE {
    return symbolicSampleSize == kSample32 ? kResultOk : kResultFalse;
  }

  tresult PLUGIN_API setBusArrangements( SpeakerArrangement *inputs, int32 numIns,
                                       SpeakerArrangement *outputs, int32 numOuts ) SMTG_OVERRIDE {
    if ( numIns == 0 && numOuts == 1 && outputs[0] == SpeakerArr::kStereo )
      return kResultOk;
    return kResultFalse;
  }

  tresult PLUGIN_API process( ProcessData &data ) SMTG_OVERRIDE {
    if ( data.numOutputs == 0 || data.numSamples <= 0 )
      return kResultOk;

    readParameterChanges( data.inputParameterChanges );

    std::vector<connie_midi_event_t> midiEvents;
    midiEvents.reserve( 64 );

    if ( IEventList *eventList = data.inputEvents ) {
      const int32 numEvents = eventList->getEventCount();
      for ( int32 i = 0; i < numEvents; i++ ) {
        Event event {};
        if ( eventList->getEvent( i, event ) != kResultOk )
          continue;

        connie_midi_event_t midi {};
        midi.sample_offset = event.sampleOffset;
        midi.size = 0;

        switch ( event.type ) {
          case Event::kNoteOnEvent:
            midi.data[0] = static_cast<uint8_t>( 0x90 | ( event.noteOn.channel & 0x0F ) );
            midi.data[1] = event.noteOn.pitch;
            midi.data[2] = static_cast<uint8_t>( event.noteOn.velocity * 127 );
            midi.size = 3;
            break;
          case Event::kNoteOffEvent:
            midi.data[0] = static_cast<uint8_t>( 0x80 | ( event.noteOff.channel & 0x0F ) );
            midi.data[1] = event.noteOff.pitch;
            midi.data[2] = static_cast<uint8_t>( event.noteOff.velocity * 127 );
            midi.size = 3;
            break;
          case Event::kDataEvent:
            if ( event.data.size >= 1 ) {
              std::memcpy( midi.data, event.data.bytes, event.data.size > 3 ? 3 : event.data.size );
              midi.size = event.data.size > 3 ? 3 : event.data.size;
            }
            break;
          default:
            continue;
        }

        if ( midi.size > 0 )
          midiEvents.push_back( midi );
      }
    }

    auto *outL = static_cast<float *>( data.outputs[0].channelBuffers32[0] );
    auto *outR = static_cast<float *>( data.outputs[0].channelBuffers32[1] );

    connie_dsp_process( outL, outR, data.numSamples,
                        midiEvents.empty() ? nullptr : midiEvents.data(),
                        static_cast<int32_t>( midiEvents.size() ) );

    data.outputs[0].silenceFlags = 0;
    return kResultOk;
  }

  tresult PLUGIN_API setState( IBStream *state ) SMTG_OVERRIDE {
    if ( !state )
      return kResultFalse;

    IBStreamer streamer( state, kLittleEndian );
    for ( ParamID id = kParam16; id <= kParamPreset; id++ ) {
      float value = 0.f;
      if ( !streamer.readFloat( value ) )
        return kResultFalse;
      setParamNormalized( id, value );
    }
    applyAllParams();
    return kResultOk;
  }

  tresult PLUGIN_API getState( IBStream *state ) SMTG_OVERRIDE {
    if ( !state )
      return kResultFalse;

    IBStreamer streamer( state, kLittleEndian );
    for ( ParamID id = kParam16; id <= kParamPreset; id++ ) {
      if ( !streamer.writeFloat( static_cast<float>( getParamNormalized( id ) ) ) )
        return kResultFalse;
    }
    return kResultOk;
  }

  IPlugView *PLUGIN_API createView( const char * ) SMTG_OVERRIDE {
    return nullptr;
  }

  tresult PLUGIN_API setParamNormalized( ParamID tag, ParamValue value ) SMTG_OVERRIDE {
    tresult result = SingleComponentEffect::setParamNormalized( tag, value );
    if ( result == kResultOk )
      applyParam( tag, value );
    return result;
  }

  OBJ_METHODS( ConniePlugin, SingleComponentEffect )
  REFCOUNT_METHODS( SingleComponentEffect )

private:
  int sampleRate = 44100;

  void syncParamsFromEngine() {
    for ( int i = 0; i < CONNIE_NUM_DRAWBARS; i++ ) {
      ParamID id = kParam16 + i;
      ParamValue norm = static_cast<ParamValue>( connie_params_get_drawbar( i ) ) / kDrawbarSteps;
      SingleComponentEffect::setParamNormalized( id, norm );
    }
    SingleComponentEffect::setParamNormalized( kParamMaster, tg_master_vol );
    SingleComponentEffect::setParamNormalized( kParamTranspose,
      static_cast<ParamValue>( transpose + 12 ) / kTransposeSteps );
    SingleComponentEffect::setParamNormalized( kParamPreset,
      static_cast<ParamValue>( connie_params_get_program() ) / kPresetSteps );
  }

  void applyParam( ParamID id, ParamValue value ) {
    if ( id >= kParam16 && id <= kParamReverb ) {
      applyDrawbarParam( id, value );
      connie_params_apply_volumes();
    } else if ( id == kParamMaster ) {
      tg_master_vol = static_cast<float>( value );
    } else if ( id == kParamTranspose ) {
      transpose = static_cast<int>( std::lround( value * kTransposeSteps ) ) - 12;
    } else if ( id == kParamPreset ) {
      int prog = static_cast<int>( std::lround( value * kPresetSteps ) );
      connie_params_set_program( prog );
      syncDrawbarParamsFromEngine();
    }
  }

  void syncDrawbarParamsFromEngine() {
    for ( int i = 0; i < CONNIE_NUM_DRAWBARS; i++ ) {
      ParamID id = kParam16 + i;
      ParamValue norm = static_cast<ParamValue>( connie_params_get_drawbar( i ) ) / kDrawbarSteps;
      SingleComponentEffect::setParamNormalized( id, norm );
    }
  }

  void applyAllParams() {
    for ( ParamID id = kParam16; id <= kParamPreset; id++ )
      applyParam( id, getParamNormalized( id ) );
    connie_params_apply_volumes();
  }

  void readParameterChanges( IParameterChanges *changes ) {
    if ( !changes )
      return;

    const int32 num = changes->getParameterCount();
    for ( int32 i = 0; i < num; i++ ) {
      if ( IParamValueQueue *queue = changes->getParameterData( i ) ) {
        const int32 points = queue->getPointCount();
        if ( points > 0 ) {
          ParamValue value = 0;
          int32 offset = 0;
          if ( queue->getPoint( points - 1, offset, value ) == kResultOk )
            applyParam( queue->getParameterId(), value );
        }
      }
    }
  }
};

} // namespace Vst
} // namespace Steinberg

using namespace Steinberg::Vst;

//------------------------------------------------------------------------
BEGIN_FACTORY_DEF( "Ho-Ro", "", "" )

  DEF_CLASS2( INLINE_UID_FROM_FUID( ConnieProcessorUID ),
              PClassInfo::kManyInstances,
              kVstAudioEffectClass,
              "Connie",
              0,
              "Instrument|Synth",
              FULL_VERSION_STR,
              kVstVersionString,
              ConniePlugin::createInstance )

END_FACTORY
