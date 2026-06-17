#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Steinberg {
namespace Vst {

static const FUID ConnieProcessorUID (0xA1B2C3D4, 0xE5F60718, 0x9A0B1C2D, 0x3E4F5061);
static const FUID ConnieControllerUID (0xB2C3D4E5, 0xF6071829, 0xAB0C1D2E, 0x4F506172);

#define ConnieVST3Category "Instrument|Synth"

} // namespace Vst
} // namespace Steinberg
