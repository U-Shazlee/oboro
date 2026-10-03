#!/usr/bin/env bash
# Fetch the third-party sources the build needs into third_party/, pinned to
# the commits Moonlight-N3DS builds with. Safe to run again: existing
# checkouts are left alone.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/../third_party"

# fetch <dir> <url> <commit> [submodules]
fetch() {
    if [ -d "$1/.git" ]; then
        echo "$1: already fetched"
        return
    fi
    echo "$1: fetching $3"
    git init -q "$1"
    git -C "$1" remote add origin "$2"
    git -C "$1" fetch -q --depth 1 origin "$3"
    git -C "$1" checkout -q FETCH_HEAD
    if [ "${4:-}" = "submodules" ]; then
        git -C "$1" submodule update -q --init --depth 1
    fi
}

# The Moonlight protocol itself (its enet fork is a submodule).
fetch moonlight-common-c https://github.com/moonlight-stream/moonlight-common-c.git 6250fa29ee87873716045e3b64f1f229374324e8 submodules
# OpenSSL with a 3DS target, for pairing (certificate, signatures, AES).
fetch openssl https://github.com/zoeyjodon/openssl.git 0c562065a2d1d52f8abfe2d836c6d5bf72dffc5c
# XML parser for the host's answers.
fetch libexpat https://github.com/libexpat/libexpat.git 654d2de0da85662fcc7644a7acd7c2dd2cfb21f0
