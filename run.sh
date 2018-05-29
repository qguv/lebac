#!/bin/bash
(
    cd termbox
    ./waf configure --prefix=.
    ./waf
)
CC="ccache gcc"
ERROR_OPTS="-Wall -Wpedantic -Wextra -Werror"
LIBS="-Itermbox/src -Ltermbox/build/src -ltermbox"
OUTPUT="/tmp/bac"
rm -f $OUTPUT && $CC $LIBS $ERROR_OPTS -o /tmp/bac main.c && strace -E LD_LIBRARY_PATH=$LD_LIBRARY_PATH:termbox/build/src /tmp/bac "$@" 2> /tmp/bac_$(date +%s).log
