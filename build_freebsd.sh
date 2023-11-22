#!/bin/sh
test `uname` != "FreeBSD" && echo "must be using FreeBSD to use this script!!" 1>&2 && exit 1

if test "$1" = "clean" || test "$1" = "distclean"
then
	rm -f *.o *.a lsuhwi
	exit 0
fi

test -z "$ENABLE_PCI_DB" || CFLAGS="$CFLAGS -DUHWI_ENABLE_PCI_DB=1"

set -ve

for fn in uhwi.c lsuhwi.c
do
	clang -c -o "`basename "$fn" .c`.o" -std=c99 -I. $CFLAGS "$fn"
done

ar crs libuhwi.a *.o
clang -o lsuhwi lsuhwi.o -L. -luhwi -lusb

exit 0
