#!/bin/sh
# arch-tag: Toplevel autotools bootstrapping script
# Run this to generate all the initial makefiles, etc.

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

ORIGDIR=`pwd`
cd $srcdir
PROJECT=rhythmbox
TEST_TYPE=-d
FILE=shell

DIE=0

if glib-gettextize --version < /dev/null > /dev/null 2>&1 ; then
	gettextize_version=`glib-gettextize --version | sed 's/^[^0-9]*\([0-9.][0-9.]*\).*/\1/'`
	case $gettextize_version in
		2.*)
		have_gettextize=true
		;;
	esac
fi
if $have_gettextize ; then : ; else
	echo
	echo "You must have glib 2.0 or later installed to compile $PROJECT."
	echo "Install the appropriate package for your distribution,"
	echo "or get the source tarball at ftp://ftp.gnome.org/pub/GNOME/"
	DIE=1
fi

have_intltoolize=false
if intltoolize --version < /dev/null > /dev/null 2>&1 ; then
	intltool_version=`intltoolize --version | sed 's/^[^0-9]*\([0-9.][0-9.]*\).*/\1/'`
	case $intltool_version in
	    0.2[5-9]*)
		have_intltoolize=true
		;;
	esac
fi
if $have_intltoolize ; then : ; else
	echo
	echo "You must have intltool 0.25 installed to compile $PROJECT."
	echo "Install the appropriate package for your distribution,"
	echo "or get the source tarball at ftp://ftp.gnu.org/pub/gnu/"
	DIE=1
fi

have_autoconf=false
if autoconf --version < /dev/null > /dev/null 2>&1 ; then
	autoconf_version=`autoconf --version | sed 's/^[^0-9]*\([0-9.][0-9.]*\).*/\1/'`
	case $autoconf_version in
	    2.5*)
		have_autoconf=true
		;;
	esac
fi
if $have_autoconf ; then : ; else
	echo
	echo "You must have autoconf installed to compile $PROJECT."
	echo "libtool the appropriate package for your distribution,"
	echo "or get the source tarball at ftp://ftp.gnu.org/pub/gnu/"
	DIE=1
fi

if automake-1.7 --version < /dev/null > /dev/null 2>&1; then
  AUTOMAKE=automake-1.7
  ACLOCAL=aclocal-1.7
else
	echo
	echo "You must have automake >= 1.7 installed to compile $PROJECT."
	echo "Get ftp://ftp.gnu.org/pub/gnu/automake/automake-1.7.2.tar.gz"
	echo "(or a newer version if it is available)"
	DIE=1
fi

if test "$DIE" -eq 1; then
	exit 1
fi

test $TEST_TYPE $FILE || {
	echo "You must run this script in the top-level $PROJECT directory"
	exit 1
}

if test -z "$AUTOGEN_SUBDIR_MODE"; then
        if test -z "$*"; then
                echo "I am going to run ./configure with no arguments - if you wish "
                echo "to pass any to it, please specify them on the $0 command line."
        fi
fi

if test -z "$ACLOCAL_FLAGS"; then

	acdir=`$ACLOCAL --print-ac-dir`
        m4list="glib-2.0.m4 glib-gettext.m4 gtk-2.0.m4"

	for file in $m4list
	do
		if [ ! -f "$acdir/$file" ]; then
			echo "WARNING: aclocal's directory is $acdir, but..."
			echo "         no file $acdir/$file"
			echo "         You may see fatal macro warnings below."
			echo "         If these files are installed in /some/dir, set the ACLOCAL_FLAGS "
			echo "         environment variable to \"-I /some/dir\", or install"
			echo "         $acdir/$file."
			echo ""
		fi
	done
fi

$ACLOCAL -I macros $ACLOCAL_FLAGS

glib-gettextize --force
intltoolize --force --automake
libtoolize --automake

$AUTOMAKE -a
autoconf

# optionally feature autoheader
autoheader

cd $ORIGDIR

if test -z "$AUTOGEN_SUBDIR_MODE"; then
	echo "Running configure..."
    ($srcdir/configure --enable-maintainer-mode "$@" \
	 && echo -e "\nNow type 'make' to compile $PROJECT.")
fi
