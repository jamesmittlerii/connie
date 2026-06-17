# External dependencies

## VST3 SDK (Windows builds only)

The VST3 plugin requires the Steinberg VST3 SDK as a git submodule:

```bash
git submodule update --init --recursive external/vst3sdk
```

Or, on first clone:

```bash
git clone --recurse-submodules <repo-url>
```

LV2 / Raspberry Pi builds do not need this SDK (`-DCONNIE_BUILD_VST3=OFF`).
