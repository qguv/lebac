#!/bin/bash
(
    cd termbox
    echo "configuring termbox"
    ./waf configure --prefix=.
    echo "building termbox"
    ./waf
)
CC="gcc"
ERROR_OPTS="-Wall -Wpedantic -Wextra -Werror"
LIBS="-Itermbox/src -Ltermbox/build/src -ltermbox"
OUTPUT="/tmp/bac"
echo "removing old bin"
rm -f $OUTPUT && \
echo "building le bac" && \
$CC $LIBS $ERROR_OPTS -o /tmp/lebac main.c && \
echo "running le bac" && \
DYLD_LIBRARY_PATH=$DYLD_LIBRARY_PATH:termbox/build/src LD_LIBRARY_PATH=$LD_LIBRARY_PATH:termbox/build/src /tmp/lebac "$@"
