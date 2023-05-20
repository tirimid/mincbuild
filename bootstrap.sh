CC=gcc
CFLAGS="-std=c99 -pedantic -Iinclude"
LIBS="-ljson-c -lpthread -ltmcul"

OUT_NAME=mincbuild

$CC $CFLAGS -o $OUT_NAME src/*.c $LIBS
