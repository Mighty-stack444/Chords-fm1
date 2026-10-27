#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# CHORDS in the browser: the host simulator compiled to WebAssembly, plus its page.
#   [OM_VERSION=0.1] web/emu/build.sh  ->  build/emu/{index.html, worklet.js, omni.wasm}
# With Emscripten (emcc) if it is installed; otherwise with clang and a WASI sysroot:
#   WASI_SYSROOT=.../wasi-sysroot-24.0 WASI_RT=.../libclang_rt.builtins-wasm32.a web/emu/build.sh
# (both from https://github.com/WebAssembly/wasi-sdk/releases). Same sources and flags as
# host/build_host.sh (-ffp-contract=off, like the device).
set -e
cd "$(dirname "$0")/../.."
mkdir -p build/gen build/emu
[ -f build/gen/felucca_font.h ] || python3 tools/gen_font.py build/gen/felucca_font.h >/dev/null
VER="-DOM_VERSION=\"$(printf %s "${OM_VERSION:-DEV}" | tr a-z A-Z)\""
WARN="-Wall -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable"
SRCS="web/emu/omni_web.c firmware/src/dsp/omni.c"
if command -v emcc >/dev/null 2>&1; then
    emcc -O2 -ffp-contract=off -std=gnu99 $WARN -DOM_HOST -DOM_WEB "$VER" -Ifirmware/src -Ifirmware/src/dsp -Ibuild/gen \
        --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=33554432 -sFILESYSTEM=0 \
        -o build/emu/omni.wasm $SRCS
else
    [ -d "${WASI_SYSROOT:-}" ] || { echo "web/emu/build.sh: no emcc, and WASI_SYSROOT is not set"; exit 1; }
    RES=build/wasi-rt
    mkdir -p "$RES/lib/wasi"
    [ -z "${WASI_RT:-}" ] || cp "$WASI_RT" "$RES/lib/wasi/libclang_rt.builtins-wasm32.a"
    CLANG_INC="$(clang -print-resource-dir)/include"
    clang --target=wasm32-wasi --sysroot="$WASI_SYSROOT" -resource-dir="$RES" -isystem "$CLANG_INC" -mexec-model=reactor \
        -O2 -ffp-contract=off -std=gnu99 $WARN -DOM_HOST -DOM_WEB "$VER" -Ifirmware/src -Ifirmware/src/dsp -Ibuild/gen \
        -Wl,--export-dynamic -Wl,-z,stack-size=1048576 -Wl,--initial-memory=33554432 -Wl,--gc-sections \
        -o build/emu/omni.wasm $SRCS
fi
cp web/emu/index.html web/emu/worklet.js build/emu/
echo "emu: build/emu ($(wc -c < build/emu/omni.wasm) B wasm)"
