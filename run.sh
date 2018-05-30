#!/bin/bash
(
    cd termbox
    echo "configuring termbox"
    ./waf configure --out=../build/termbox
    echo "building termbox"
    ./waf
)

CC="gcc"
ERROR_OPTS="-Wall -Wpedantic -Wextra -Werror"
LIBS="-Itermbox/src -Lbuild/termbox/src -ltermbox"
OUTPUT="build/lebac"

echo "removing old bin"
rm -f $OUTPUT && \
echo "building le bac" && \
$CC $LIBS $ERROR_OPTS -o "$OUTPUT" src/main.c && \
echo "running le bac" && \
DYLD_LIBRARY_PATH=$DYLD_LIBRARY_PATH:build/termbox/src LD_LIBRARY_PATH=$LD_LIBRARY_PATH:build/termbox/src "$OUTPUT" "$@"
