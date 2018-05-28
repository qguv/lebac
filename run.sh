#!/bin/bash
(
    cd termbox
    ./waf configure --prefix=.
    ./waf
)
CC="ccache gcc"
ERROR_OPTS="-Wall -Wpedantic -Wextra -Werror"
LIBS="-I termbox/src -L termbox/build/src -l termbox -l out123"
OUTPUT="/tmp/bac"
rm -f $OUTPUT && $CC $LIBS $ERROR_OPTS -o /tmp/bac main.c && LD_LIBRARY_PATH=$LD_LIBRARY_PATH:termbox/build/src /tmp/bac "$@"
