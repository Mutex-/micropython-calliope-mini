#!/bin/sh
#
# Regenerate inc/genhdr/qstrdefs.generated.h and keep the build consistent.
#
# Usage (from the repository root, normally via make): tools/updateqstrs.sh <build-dir>
#
# - Table unchanged: the old header is put back with its original timestamp,
#   so nothing gets recompiled.
# - Table changed: <build-dir> is removed. New names shift every qstr number,
#   and an incremental build keeps some objects compiled against the old
#   table; that firmware hangs at boot without a REPL.

BUILD_DIR="$1"
GEN=inc/genhdr/qstrdefs.generated.h
GENHDRDIR=inc/genhdr

if [ -z "$BUILD_DIR" ]; then
    echo "usage: $0 <build-dir>" >&2
    exit 2
fi

OLD=$(mktemp) || exit 1
cp -p "$GEN" "$OLD" || exit 1

cleanup() {
    rm -rf "$GENHDRDIR/qstr" "$GENHDRDIR/qstr.i.last" "$GENHDRDIR/qstrdefs.collected.h" \
           "$GENHDRDIR/qstrdefs.collected.h.hash" "$GENHDRDIR/qstrdefs.preprocessed.h"
    rm -f "$OLD"
}

if ! ./tools/makeqstrhdr.sh > /dev/null; then
    cp -p "$OLD" "$GEN"
    cleanup
    echo "qstrs: generation failed, kept the previous table" >&2
    exit 1
fi

if cmp -s "$GEN" "$OLD"; then
    cp -p "$OLD" "$GEN"
    echo "qstrs: unchanged"
else
    echo "qstrs: table changed, removing $BUILD_DIR for a clean build"
    rm -rf "$BUILD_DIR"
fi
cleanup
