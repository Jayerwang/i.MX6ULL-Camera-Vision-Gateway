#!/bin/sh

set -eu

CROSS_COMPILE=${CROSS_COMPILE:-arm-linux-gnueabihf-}
BINARY=${1:-build/ov5640_capture}
CC="${CROSS_COMPILE}gcc"
READELF="${CROSS_COMPILE}readelf"

if [ ! -f "$BINARY" ]; then
    echo "Binary not found: $BINARY" >&2
    echo "Build it first, for example:" >&2
    echo "  make CROSS_COMPILE=${CROSS_COMPILE} USE_LIBJPEG=1 STATIC=0" >&2
    exit 1
fi

SYSROOT=$($CC -print-sysroot)
if [ -z "$SYSROOT" ]; then
    echo "Failed to get sysroot from $CC" >&2
    exit 1
fi

echo "Binary:  $BINARY"
echo "Sysroot: $SYSROOT"
echo

echo "ELF interpreter and needed libraries:"
$READELF -l "$BINARY" | sed -n '/interpreter/p'
$READELF -d "$BINARY" | sed -n 's/.*Shared library: \[\(.*\)\]/  \1/p'
echo

echo "Searching sysroot for needed libraries:"
$READELF -d "$BINARY" |
    sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' |
    while read -r lib; do
        found=$(find "$SYSROOT" -name "$lib" -o -name "$lib.*" 2>/dev/null | head -n 1 || true)
        if [ -n "$found" ]; then
            echo "  OK      $lib -> $found"
        else
            echo "  MISSING $lib"
        fi
    done

echo
echo "If libjpeg is missing on the board, copy the ARM runtime library:"
echo "  find \"$SYSROOT\" -name 'libjpeg.so*'"
echo "  adb push /path/to/libjpeg.so.X /usr/lib/"
