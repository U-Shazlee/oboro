#!/usr/bin/env bash
# Build libexpat and OpenSSL for the 3DS and install them into devkitPro's
# portlibs, as Moonlight-N3DS does (these are its build scripts, merged).
# Needs devkitARM, autoconf, libtool and perl. Run tools/fetch-deps.sh first.
set -euo pipefail

: "${DEVKITPRO:?Set DEVKITPRO (source /etc/profile.d/devkit-env.sh)}"
: "${DEVKITARM:?Set DEVKITARM (source /etc/profile.d/devkit-env.sh)}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORTLIBS="$DEVKITPRO/portlibs/3ds"
JOBS="$(nproc 2>/dev/null || echo 2)"

if [ ! -f "$PORTLIBS/lib/libexpat.a" ]; then
    cd "$ROOT_DIR/third_party/libexpat/expat"
    export CFLAGS="-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -mword-relocations -Wno-psabi -fomit-frame-pointer -ffunction-sections"
    export CXXFLAGS="$CFLAGS"
    export CPPFLAGS="-D__3DS__ -I$DEVKITPRO/libctru/include"
    export LDFLAGS="-L$DEVKITPRO/libctru/lib"
    export LIBS="-lctru -lm"
    ./buildconf.sh
    ./configure \
        --prefix="$PORTLIBS/" \
        --host=arm-none-eabi \
        --enable-static \
        --without-examples \
        --without-tests \
        --without-docbook \
        CC="$DEVKITARM/bin/arm-none-eabi-gcc" \
        CXX="$DEVKITARM/bin/arm-none-eabi-g++" \
        AR="$DEVKITARM/bin/arm-none-eabi-ar" \
        RANLIB="$DEVKITARM/bin/arm-none-eabi-ranlib" \
        PKG_CONFIG="$PORTLIBS/bin/arm-none-eabi-pkg-config"
    make -j"$JOBS"
    make install
    unset CFLAGS CXXFLAGS CPPFLAGS LDFLAGS LIBS
fi

if [ ! -f "$PORTLIBS/lib/libcrypto.a" ]; then
    cd "$ROOT_DIR/third_party/openssl"
    ./Configure 3ds \
        no-threads no-shared no-asm no-ui-console no-unit-test no-tests no-buildtest-c++ no-external-tests no-autoload-config \
        --with-rand-seed=os -static -Wno-implicit-function-declaration -Wno-incompatible-pointer-types -Wno-int-conversion
    make build_generated -j"$JOBS"
    make libssl.a libcrypto.a -j"$JOBS"
    cp libssl.a libcrypto.a "$PORTLIBS/lib/"
    mkdir -p "$PORTLIBS/include/openssl" "$PORTLIBS/include/crypto"
    cp -r include/openssl/. "$PORTLIBS/include/openssl/"
    cp -r include/crypto/. "$PORTLIBS/include/crypto/"
fi

echo "libexpat and OpenSSL are installed in $PORTLIBS"
