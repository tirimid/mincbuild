CC=gcc
CFLAGS="-std=c99 -pedantic -Iinclude -D_POSIX_C_SOURCE=200809"
LIBS="-lpthread"

OUT_NAME=mincbuild

$CC $CFLAGS -o $OUT_NAME src/*.c $LIBS
