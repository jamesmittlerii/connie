# Windows LV2 Build

Build Connie's LV2 plugin from an MSYS2 UCRT64 shell. This build only needs
the LV2 target; disable VST3 so the Steinberg SDK/submodules are not required.

## Prerequisites

```sh
pacman -S --needed \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja
```

## Configure

```sh
cd /c/Users/chica/git/connie

cmake -S . -B build-ucrt64-lv2 -G Ninja \
  -DCONNIE_BUILD_LV2=ON \
  -DCONNIE_BUILD_VST3=OFF \
  -DCONNIE_LV2_BUNDLE_DIR="$HOME/.lv2/connie.lv2"
```

## Build And Test

```sh
cmake --build build-ucrt64-lv2 --target connie-lv2
cmake --build build-ucrt64-lv2 --target test-headless
```

The build tree bundle is written to:

```text
build-ucrt64-lv2/connie.lv2
```

It should contain:

```text
connie.dll
connie.ttl
manifest.ttl
```

For a Windows build, `manifest.ttl` should point at the DLL:

```sh
grep -R "lv2:binary" build-ucrt64-lv2/connie.lv2/*.ttl
```

Expected output includes `connie.dll`, not `connie.so`.

## Install

```sh
cmake --install build-ucrt64-lv2
```

The install target copies the bundle to:

```text
~/.lv2/connie.lv2
```

If your LV2 host does not scan `~/.lv2` automatically, add it to `LV2_PATH`
before launching the host:

```sh
export LV2_PATH="$HOME/.lv2${LV2_PATH:+:$LV2_PATH}"
```
