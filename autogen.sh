#!/bin/sh
# Run this to generate all the initial makefiles, etc.

test -n "$srcdir" || srcdir=`dirname "$0"`
test -n "$srcdir" || srcdir=.

olddir=`pwd`
cd "$srcdir"

INTLTOOLIZE=`which intltoolize`
if test -z $INTLTOOLIZE; then
        echo "*** No intltoolize found, please install the intltool package ***"
        exit 1
fi

AUTORECONF=`which autoreconf`
if test -z $AUTORECONF; then
        echo "*** No autoreconf found, please install it ***"
        exit 1
fi

GTKDOCIZE=`which gtkdocize`
if test -z $GTKDOCIZE; then
	echo "*** No GTK-Doc found, please install it ***"
	exit 1
fi

if test -z `which autopoint`; then
        echo "*** No autopoint found, please install it ***"
        exit 1
fi


ACDIR=`${ACLOCAL:-aclocal} --print-ac-dir`
if ! test -f $ACDIR/yelp.m4; then
	echo "*** No yelp-tools found, please install it ***"
	exit 1
fi

# Need to mkdir -p the m4 directory in case it doesn't exist, to prevent
# gtkdocize from failing.
mkdir -p m4
gtkdocize || exit $?
autopoint --force
AUTOPOINT='intltoolize --automake --copy' autoreconf --force --install --verbose

cd "$olddir"
test -n "$NOCONFIGURE" || "$srcdir/configure" --enable-uninstalled-build "$@"

