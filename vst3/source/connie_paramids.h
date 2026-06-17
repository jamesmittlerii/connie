#pragma once

#include "pluginterfaces/vst/vsttypes.h"

namespace Steinberg {
namespace Vst {

enum ConnieParamIDs : ParamID {
  kParam16 = 100,
  kParam8,
  kParam4,
  kParamIV,
  kParamFlute,
  kParamReed,
  kParamSharp,
  kParamPerc,
  kParamVibrato,
  kParamReverb,
  kParamMaster,
  kParamTranspose,
  kParamPreset
};

} // namespace Vst
} // namespace Steinberg
