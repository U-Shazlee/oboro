#!/usr/bin/env bash
# Type-check Oboro's own sources on any PC, without devkitPro: every file in
# source/ is compiled for 32-bit ARM against the real libctru, citro2d,
# moonlight-common-c, curl, jansson, Opus, mbedTLS and zlib headers, then all
# objects are linked relocatably to catch duplicate symbols.
#
# It proves the code compiles and names what it calls correctly. It does not
# produce a .3dsx and says nothing about how it runs on a console.
#
# Needs: git, python with `pip install ziglang`. Headers are cached in
# $OBORO_CHECK_DIR (default: a folder in the system temp directory).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIR="${OBORO_CHECK_DIR:-${TMPDIR:-/tmp}/oboro-typecheck}"
mkdir -p "$DIR/inc/shim/sys" "$DIR/inc/opus" "$DIR/obj"

clone() { [ -d "$DIR/$1" ] || git clone -q --depth 1 ${3:-} "$2" "$DIR/$1"; }
clone libctru https://github.com/devkitPro/libctru
clone citro3d https://github.com/devkitPro/citro3d
clone citro2d https://github.com/devkitPro/citro2d
clone jansson https://github.com/akheron/jansson
clone opus https://github.com/xiph/opus
clone zlib https://github.com/madler/zlib
clone curl https://github.com/curl/curl
clone mbedtls https://github.com/Mbed-TLS/mbedtls "--branch v2.28.8"
MOON="$ROOT/third_party/moonlight-common-c"
if [ ! -f "$MOON/src/Limelight.h" ]; then
    MOON="$DIR/moonlight-common-c"
    [ -d "$MOON" ] || git clone -q https://github.com/moonlight-stream/moonlight-common-c "$MOON"
    git -C "$MOON" checkout -q 6250fa29ee87873716045e3b64f1f229374324e8 2>/dev/null || true
fi

cp "$DIR"/opus/include/*.h "$DIR/inc/opus/"
cp "$DIR/jansson/src/jansson.h" "$DIR/zlib/zlib.h" "$DIR/zlib/zconf.h" "$DIR/inc/"
sed -e 's/@json_inline@/inline/; s/@json_have_long_long@/1/; s/@json_have_localeconv@/0/' \
    -e 's/@json_have_atomic_builtins@/1/; s/@json_have_sync_builtins@/1/' \
    "$DIR/jansson/src/jansson_config.h.in" > "$DIR/inc/jansson_config.h"
# newlib's lock types, which libctru's headers expect from the 3DS toolchain.
cat > "$DIR/inc/shim/sys/lock.h" <<'EOF'
#pragma once
#include <stdint.h>
typedef int32_t _LOCK_T;
typedef struct { _LOCK_T lock; uint32_t thread_tag; uint32_t counter; } _LOCK_RECURSIVE_T;
EOF

cd "$ROOT"
rm -f "$DIR"/obj/*.o
failed=0
for f in source/*.c libgamestream/http.c; do
    out="$(python -m ziglang cc -target arm-linux-musleabihf -c -o "$DIR/obj/$(basename "$f").o" \
        -std=gnu11 -Wall -Wextra -Wno-unused-parameter -D__3DS__ -D_GNU_SOURCE -DUSE_MBEDTLS '-DAPP_VERSION="check"' \
        -Iinclude -Ilibgamestream -Ithird_party/libuuid -I"$MOON/src" \
        -I"$DIR/inc/shim" -I"$DIR/libctru/libctru/include" -I"$DIR/citro3d/include" -I"$DIR/citro2d/include" \
        -I"$DIR/inc" -I"$DIR/curl/include" -I"$DIR/mbedtls/include" "$f" 2>&1 | grep -E "error|warning" | grep -v "vendor/stb" || true)"
    if [ -n "$out" ]; then
        echo "$out"
        failed=1
    fi
done
[ "$failed" = 0 ] || { echo "typecheck: FAILED"; exit 1; }
python -m ziglang cc -target arm-linux-musleabihf -r -o "$DIR/obj/all.o" "$DIR"/obj/*.c.o
echo "typecheck: OK ($(ls "$DIR"/obj/*.c.o | wc -l) files, no errors, no warnings, no duplicate symbols)"
