AC_PREREQ(2.63.2)

AC_INIT([rhythmbox],
	[3.4.2],
	[https://bugzilla.gnome.org/enter_bug.cgi?product=rhythmbox])
AC_CONFIG_SRCDIR(rhythmbox.pc.in)
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS(config.h)
AM_INIT_AUTOMAKE([1.11 tar-ustar dist-xz no-dist-gzip -Wno-portability])

AM_MAINTAINER_MODE([enable])

# plugin API versioning
# Increment on interface addition, reset on removal
RHYTHMBOX_CORE_AGE=0
# Increment on interface add, remove, or change
RHYTHMBOX_CORE_CURRENT=10
# Increment on source change, reset when CURRENT changes.
RHYTHMBOX_CORE_REVISION=0

AC_SUBST(RHYTHMBOX_CORE_AGE)
AC_SUBST(RHYTHMBOX_CORE_CURRENT)
AC_SUBST(RHYTHMBOX_CORE_REVISION)

AM_SILENT_RULES([yes])

dnl XXXX hack to kill off all the libtool tags ...
dnl it isn't like we are using C++ or Fortran.
m4_define([_LT_AC_TAGCONFIG],[])

GOBJECT_INTROSPECTION_REQS=0.10.0
GOBJECT_INTROSPECTION_REQUIRE([$GOBJECT_INTROSPECTION_REQS])

IT_PROG_INTLTOOL([0.35.0])

AC_ISC_POSIX
AC_PROG_CC
AC_STDC_HEADERS
LT_INIT([disable-static])
AM_PROG_LIBTOOL
m4_ifdef([LT_OUTPUT], [LT_OUTPUT])
AC_C_BIGENDIAN
AC_CHECK_SIZEOF(long)

GTK_REQS=3.20.0

GST_REQS=1.4.0
GDK_PIXBUF_REQS=2.18.0
GLIB_REQS=2.38.0
LIBGPOD_REQS=0.6
TOTEM_PLPARSER_REQS=3.2.0
VALA_REQS=0.9.4
LIBSOUP_REQS=2.42.0
GUDEV_REQS=143
LIBMTP_REQS=0.3.0
LIBPEAS_REQS=0.7.3
GRILO_REQS=0.3.0
LIBXML2_REQS=2.7.8
LIBSECRET_REQS=0.18

LIBNOTIFY_REQS=0.7.0
BRASERO_MIN_REQS=2.31.5
PYGOBJECT_REQUIRED=3.0.0

GLIB_GSETTINGS



AC_MSG_CHECKING([for GNU extension fwrite_unlocked])
AC_LINK_IFELSE(
[AC_LANG_PROGRAM([[
#define _GNU_SOURCE
#include <stdio.h>
]],
[[fwrite_unlocked ("foo", 1, sizeof ("foo"), stdout);]])],[have_fwrite_unlocked=yes])
if test x"$have_fwrite_unlocked" = xyes; then
	AC_DEFINE(HAVE_GNU_FWRITE_UNLOCKED,1,[Define if you have GNU fwrite_unlocked])
	AC_MSG_RESULT([yes])
else
	AC_MSG_RESULT([no])
fi

PKG_PROG_PKG_CONFIG

PKG_CHECK_MODULES(RB_CLIENT, glib-2.0 >= $GLIB_REQS gio-2.0 >= $GLIB_REQS gio-unix-2.0 >= $GLIB_REQS)

dnl Always require libX11 for XInitThreads() on X11 systems
X11_PKG=""
gdk_targets=`$PKG_CONFIG --variable targets gdk-3.0`
for target in $gdk_targets; do
	case "$target" in
		x11)
			X11_PKG=x11
			break
			;;
		*)
			;;
	esac
done

dnl  note: gio-unix-2.0 is here for libmediaplayerid
PKG_CHECK_MODULES(RHYTHMBOX,				\
		  gobject-introspection-1.0 >= $GOBJECT_INTROSPECTION_REQS \
		  gtk+-3.0 >= $GTK_REQS			\
		  $X11_PKG				\
		  gdk-pixbuf-2.0 >= $GDK_PIXBUF_REQS	\
		  glib-2.0 >= $GLIB_REQS		\
		  gio-2.0 >= $GLIB_REQS			\
		  gio-unix-2.0 >= $GLIB_REQS		\
		  libsoup-2.4 >= $LIBSOUP_REQS		\
		  libpeas-1.0 >= $LIBPEAS_REQS		\
		  libpeas-gtk-1.0 >= $LIBPEAS_REQS	\
		  libxml-2.0 >= $LIBXML2_REQS		\
		  tdb >= 1.2.6				\
		  json-glib-1.0)

PKG_CHECK_MODULES(TOTEM_PLPARSER, totem-plparser >= $TOTEM_PLPARSER_REQS, have_totem_plparser=yes, have_totem_plparser=no)
if test x$have_totem_plparser != xyes; then
   AC_MSG_ERROR([totem playlist parsing library not found or too old])
else
   if test "x`$PKG_CONFIG --variable uselibcamel totem-plparser`" = "xno" ; then
       AC_MSG_ERROR([totem playlist parsing library has Podcast parsing disabled])
   fi
fi

dnl gudev
AC_ARG_WITH(gudev,
	    AC_HELP_STRING([--with-gudev],
			   [Enable GUdev for hardware detection]),,
	    with_gudev=auto)
if test x"$with_gudev" != "xno"; then
	PKG_CHECK_MODULES(GUDEV, gudev-1.0 >= $GUDEV_REQS,
			  have_gudev=yes, have_gudev=no)
	if test x"$have_gudev" = "xno" -a x"$with_gudev" = "xyes"; then
	  AC_MSG_ERROR([GUdev explicitly requested, but is not available])
	fi
	if test x"$have_gudev" = "xyes"; then
	  use_gudev=yes
	  AC_SUBST(GUDEV_CFLAGS)
	  AC_SUBST(GUDEV_LIBS)
	  AC_DEFINE(HAVE_GUDEV, 1, [Use GUdev for hardware detection])
	else
	  use_gudev=no
	fi
else
  use_gudev=no
fi
AM_CONDITIONAL(USE_GUDEV, test x"$use_gudev" = xyes)

dnl iPod support
AC_ARG_WITH(ipod,
            AC_HELP_STRING([--with-ipod],
			   [Enable iPod support]),,
	      with_ipod=auto)
if test "x$with_ipod" != "xno"; then
	PKG_CHECK_MODULES(IPOD, libgpod-1.0 >= $LIBGPOD_REQS,
	                  have_libgpod=yes, have_libgpod=no)
	if test "x$have_libgpod" = "xno" -a "x$with_ipod" = "xyes"; then
	  AC_MSG_ERROR([iPod explicitly requested but libgpod couldn't be found])
	fi
	if test "x$have_libgpod" = "xyes"; then
	  PKG_CHECK_EXISTS(libgpod-1.0 >= 0.7.92, have_itdb_start_stop_sync=yes,
						  have_itdb_start_stop_sync=no)
          if test "x$have_itdb_start_stop_sync" = "xyes"; then
	    AC_DEFINE([HAVE_ITDB_START_STOP_SYNC], 1, [Indicates whether libgpod is new enough to have its itdb_start/stop_sync API])
          fi
	  use_ipod=yes
	  AC_SUBST(IPOD_CFLAGS)
	  AC_SUBST(IPOD_LIBS)
	fi
fi
AM_CONDITIONAL(USE_IPOD, test x"$use_ipod" = xyes)

dnl mtp support
AC_ARG_WITH(mtp,
            AC_HELP_STRING([--with-mtp],
			   [Enable MTP support]),,
	      with_mtp=auto)
if test "x$with_mtp" != "xno"; then
	can_use_mtp=no
	if test x"$use_gudev" = "xyes"; then
	  can_use_mtp=yes
	fi

	PKG_CHECK_MODULES(MTP, libmtp >= $LIBMTP_REQS, have_libmtp=yes, have_libmtp=no)
	if test x"$with_mtp" = "xyes"; then
	  if test x"$have_libmtp" = "xno"; then
	    AC_MSG_ERROR([MTP explicitly requested but libmtp is not available])
	  fi
	  if test x"$can_use_mtp" = "xno"; then
	    AC_MSG_ERROR([MTP explicitly requested but GUdev is not available])
	  fi
	fi
	if test "x$have_libmtp" = "xyes" -a "x$can_use_mtp" = "xyes"; then
	  use_mtp=yes
	  AC_SUBST(MTP_CFLAGS)
	  AC_SUBST(MTP_LIBS)
	fi
fi
AM_CONDITIONAL(USE_MTP, test x"$use_mtp" = xyes)


dnl libsecret keyring support

AC_ARG_WITH(libsecret,
            AC_HELP_STRING([--with-libsecret],
			   [Enable keyring support using libsecret]),,
	      with_libsecret=auto)
if test "x$with_libsecret" != "xno"; then

	PKG_CHECK_MODULES(LIBSECRET, libsecret-1 >= $LIBSECRET_REQS,
			  have_libsecret=yes, have_libsecret=no)
	if test "x$have_libsecret" = "xno" -a "x$with_libsecret" = "xyes"; then
	  AC_MSG_ERROR([keyring support explicitly requested but libsecret
			could not be found])
	fi
	if test "x$have_libsecret" = "xyes"; then
	   AC_DEFINE(WITH_LIBSECRET, 1, [Define if libsecret support is enabled])
	   use_libsecret=yes
	  AC_SUBST(LIBSECRET_CFLAGS)
	  AC_SUBST(LIBSECRET_LIBS)
	  AC_SUBST(PY_LIBSECRET_ENABLED, True)
	else
	  AC_SUBST(PY_LIBSECRET_ENABLED, False)
	fi
else
	AC_SUBST(PY_LIBSECRET_ENABLED, False)
fi
AM_CONDITIONAL(USE_LIBSECRET, test x"$use_libsecret" = xyes)

dnl Database
AC_ARG_WITH(database,
              AC_HELP_STRING([--with-database=tree],
			     [Select the database to use (default tree)]),,
	      with_database=tree)
AM_CONDITIONAL(USE_TREEDB, test x"$with_database" = xtree)

case "x$with_database" in
  "xtree")
    AC_DEFINE(WITH_RHYTHMDB_TREE, 1, [Define if you are using the RhythmDB tree database])
    ;;
  *)
    AC_MSG_ERROR([Unknown database selected])
    ;;
esac

dnl Database debugging
AC_ARG_WITH(rhythmdb-debug,
              AC_HELP_STRING([--with-rhythmdb-debug=0|1|2],
			     [Level of RhythmDB sanity checking]),,with_rhythmdb_debug=0)
if test x"${with_rhythmdb_debug}" != x0; then
   AC_DEFINE_UNQUOTED([RHYTHMDB_ENABLE_SANITY_CHECK], "${with_rhythmdb_debug}", [Define to the level of RhythmDB sanity checking])
fi

dnl Sound system
dnl Now we're ready to ask for gstreamer libs and cflags
dnl And we can also ask for the right version of gstreamer
PKG_CHECK_MODULES(GSTREAMER, \
	gstreamer-1.0 >= $GST_REQS
	gstreamer-audio-1.0 >= $GST_REQS
	gstreamer-base-1.0 >= $GST_REQS
	gstreamer-plugins-base-1.0 >= $GST_REQS
	gstreamer-pbutils-1.0 >= $GST_REQS)

RHYTHMBOX_CFLAGS="$RHYTHMBOX_CFLAGS $GSTREAMER_CFLAGS -DGST_USE_UNSTABLE_API"
RHYTHMBOX_LIBS="$RHYTHMBOX_LIBS $GSTREAMER_LIBS"

AC_ARG_ENABLE(libnotify,
            AC_HELP_STRING([--disable-libnotify],
			   [Disable libnotify support]),,
	      enable_libnotify=auto)
if test "x$enable_libnotify" != "xno"; then
	PKG_CHECK_MODULES(NOTIFY,                            \
			  libnotify >= $LIBNOTIFY_REQS,
			  have_libnotify=yes,
			  have_libnotify=no)
	if test "x$have_libnotify" = "xno" -a "x$enable_libnotify" = "xyes"; then
	  AC_MSG_ERROR([libnotify support explicitly requested, but libnotify couldn't be found])
	fi
	if test "x$have_libnotify" = "xyes"; then
	     enable_libnotify=yes
	fi
fi
AM_CONDITIONAL(USE_NOTIFY, test x"$enable_libnotify" = xyes)
if test x$enable_libnotify = xyes ; then
    # Find out the version of LIBNOTIFY we're using
    libnotify_version=`$PKG_CONFIG --modversion libnotify`
    LIBNOTIFY_VERSION_MAJOR=`echo $libnotify_version | awk -F. '{print $1}'`
    LIBNOTIFY_VERSION_MINOR=`echo $libnotify_version | awk -F. '{print $2}'`
    LIBNOTIFY_VERSION_MICRO=`echo $libnotify_version | awk -F. '{print $3}'`
    if test "z$LIBNOTIFY_VERSION_MAJOR" = "z"; then
        LIBNOTIFY_VERSION_MAJOR="0"
    fi
    if test "z$LIBNOTIFY_VERSION_MINOR" = "z"; then
        LIBNOTIFY_VERSION_MINOR="0"
    fi
    if test "z$LIBNOTIFY_VERSION_MICRO" = "z"; then
        LIBNOTIFY_VERSION_MICRO="0"
    fi
    echo "Your libnotify version is $LIBNOTIFY_VERSION_MAJOR,$LIBNOTIFY_VERSION_MINOR,$LIBNOTIFY_VERSION_MICRO."
    NOTIFY_CFLAGS="$NOTIFY_CFLAGS -DLIBNOTIFY_VERSION_MAJOR=$LIBNOTIFY_VERSION_MAJOR"
    NOTIFY_CFLAGS="$NOTIFY_CFLAGS -DLIBNOTIFY_VERSION_MINOR=$LIBNOTIFY_VERSION_MINOR"
    NOTIFY_CFLAGS="$NOTIFY_CFLAGS -DLIBNOTIFY_VERSION_MICRO=$LIBNOTIFY_VERSION_MICRO"

    AC_DEFINE(HAVE_NOTIFY, 1, [Define if libnotify support is enabled])
    AC_SUBST(NOTIFY_CFLAGS)
    AC_SUBST(NOTIFY_LIBS)
fi

AC_CHECK_LIB(z, uncompress)

AC_PATH_XTRA
CFLAGS="$CFLAGS $X_CFLAGS"
#LIBS=$X_LIBS

dnl Multimedia keys
have_xfree=no
AC_COMPILE_IFELSE([AC_LANG_SOURCE([[
	#include <X11/XF86keysym.h>
int main(int argc,char **argv) {
  return 0;
}
]])], have_xfree=yes)
AC_MSG_CHECKING(for XFree86 headers)
AC_MSG_RESULT($have_xfree)
if test x"$have_xfree" = "xyes" ; then
	AC_DEFINE(HAVE_XFREE, 1, [defined if you have X11/XF86keysym.h])
fi

AC_ARG_ENABLE(mmkeys, AC_HELP_STRING([--disable-mmkeys],
					[don't build with Multimedia Keys support]))
if test x"$have_xfree" = xyes; then
	if test x"$enable_mmkeys" != xno; then
		enable_mmkeys=yes
		AC_DEFINE(HAVE_MMKEYS, 1, [define if Multimedia Keys are enabled])
	fi
else
	if test x"$enable_mmkeys" = xyes; then
		AC_MSG_ERROR([Multimedia keys explicitly requested but no support found])
	fi
fi

dnl libbrasero-media support
have_libbrasero_media=no
AC_ARG_WITH(brasero,
	    AC_HELP_STRING([--with-brasero],
	    		   [Build with libbrasero-media support]),,
	    with_brasero=auto)
if test x"$with_brasero" != "xno"; then
    PKG_CHECK_MODULES(LIBBRASERO_MEDIA,
	libbrasero-media3 >= $BRASERO_MIN_REQS,
	have_libbrasero_media=yes,
	have_libbrasero_media=no)
    AC_SUBST(LIBBRASERO_MEDIA_CFLAGS)
    AC_SUBST(LIBBRASERO_MEDIA_LIBS)
fi
if test "x$have_libbrasero_media" = "xyes"; then
    AC_DEFINE([HAVE_LIBBRASERO_MEDIA], 1, [Have libbrasero-media])
fi
AM_CONDITIONAL(HAVE_LIBBRASERO_MEDIA, test x$have_libbrasero_media = xyes)
AC_SUBST(HAVE_LIBBRASERO_MEDIA)

AC_SUBST(CFLAGS)
AC_SUBST(LDFLAGS)

AC_PATH_PROG(GLIB_COMPILE_RESOURCES, glib-compile-resources)

AC_SUBST(RHYTHMBOX_CFLAGS)
AC_SUBST(RHYTHMBOX_LIBS)

AC_CACHE_CHECK([whether strftime supports %E and %O modifiers], ac_cv_strftime_supports_E_O, [
AC_RUN_IFELSE([AC_LANG_SOURCE([[#include <time.h>
                int main ()
                {
                  char buf[100];
                  struct tm tm = {0};
                  tm.tm_year = 99;
                  if (strftime(buf, 100, "%EY", &tm) == 4 &&
                      strcmp (buf, "1999")==0)
                    return 0;
                  return 1;
                }
            ]])],
            [ac_cv_strftime_supports_E_O=yes],
            [ac_cv_strftime_supports_E_O=no],
            [AC_MSG_RESULT([cannot run test program while cross compiling])
            AC_MSG_ERROR([Please set ac_cv_strftime_supports_E_O to yes or no.])]
            )
])
if test "x$ac_cv_strftime_supports_E_O" = xyes; then
        AC_DEFINE(HAVE_STRFTIME_EXTENSION, 1, [Define if strftime supports %E and %O modifiers.])
fi

AM_GNU_GETTEXT_VERSION([0.18])
AM_GNU_GETTEXT([external])

AC_SUBST([GETTEXT_PACKAGE], [rhythmbox])
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE], "$GETTEXT_PACKAGE", [Define to the gettext package name.])

dnl LIRC
AC_ARG_ENABLE(lirc,
	AC_HELP_STRING([--enable-lirc],[enable lirc support]))
with_lirc=no
if test x"$enable_lirc" != xno; then
	AC_CHECK_HEADER(lirc/lirc_client.h,[with_lirc=yes],[with_lirc=no])
	if test x"$with_lirc" = xyes; then
		AC_CHECK_LIB(lirc_client,lirc_init,[with_lirc=yes],[with_lirc=no])
	fi
	if test x"$with_lirc" = xno -a x"$enable_lirc" = xyes; then
		AC_MSG_ERROR([lirc explicitly requested but no support found])
	fi
fi
AM_CONDITIONAL(WITH_LIRC, test x"$with_lirc" = xyes)

AC_ARG_ENABLE(uninstalled-build,
              AC_HELP_STRING([--enable-uninstalled-build],
			     [Search for files in build directory as well]),
	      enable_uninstalled=$enableval,)
if test x"$enable_uninstalled" = xyes; then
	AC_DEFINE(USE_UNINSTALLED_DIRS, 1, [Define to look for files in source tree locations])
fi
ROOT_UNINSTALLED_DIR=$( cd $( dirname $0 ) && pwd )
AC_DEFINE_UNQUOTED(SHARE_UNINSTALLED_DIR,"$ROOT_UNINSTALLED_DIR/data",[path to source data dir])
AC_DEFINE_UNQUOTED(SHARE_UNINSTALLED_BUILDDIR,"`pwd`/data",[path to built source data dir])
AC_DEFINE_UNQUOTED(METADATA_UNINSTALLED_DIR,"`pwd`/metadata",[path to metadata build dir])
AC_SUBST(ROOT_UNINSTALLED_DIR)


dnl warnings bits, copied from gnome-keyring configure.in
dnl Turn on the additional warnings last, so -Werror doesn't affect other tests.

AC_ARG_ENABLE(more-warnings,
[  --enable-more-warnings  Maximum compiler warnings],
set_more_warnings="$enableval",[
if test -d "$srcdir/.svn" || test -d "$srcdir/{arch}" || test -d "$srcdir/CVS" || test -d "$srcdir/_darcs" || test -d "$srcdir/.git"; then
	set_more_warnings=yes
else
	set_more_warnings=no
fi
])
RHYTHMBOX_CFLAGS_NOWARN=$RHYTHMBOX_CFLAGS
AC_SUBST(RHYTHMBOX_CFLAGS_NOWARN)
AC_MSG_CHECKING(for more warnings, including -Werror)
if test "$GCC" = "yes" -a "$set_more_warnings" != "no"; then
	AC_MSG_RESULT(yes)
	WARN_CFLAGS="\
	-Wcomment -Wformat -Wnonnull -Wimplicit-int -Wimplicit \
	-Wmain -Wmissing-braces -Wparentheses -Wsequence-point \
	-Wreturn-type -Wswitch -Wtrigraphs -Wunused-function \
	-Wunused-label -Wunused-value \
	-Wchar-subscripts -Wmissing-declarations -Wmissing-prototypes \
	-Wnested-externs -Wpointer-arith \
	-Wformat-security -Wall \
	-Werror -Wno-deprecated-declarations -std=gnu89"

	if echo "$CFLAGS" | grep -q -- '-O' && echo "$CFLAGS" | grep -q -v -- '-O0'; then
		WARN_CFLAGS="-Wuninitialized $WARN_CFLAGS"
	fi

	for option in $WARN_CFLAGS; do
		SAVE_CFLAGS="$CFLAGS"
		CFLAGS="$CFLAGS $option"
		AC_MSG_CHECKING([whether gcc understands $option])
		AC_TRY_COMPILE([], [],
			has_option=yes,
			has_option=no,)
		if test x$has_option = xyes; then
			RHYTHMBOX_CFLAGS="$RHYTHMBOX_CFLAGS $option"
		fi
		AC_MSG_RESULT($has_option)
		CFLAGS="$SAVE_CFLAGS"
		unset has_option
		unset SAVE_CFLAGS
	done
	unset option
	AC_SUBST(WARN_CFLAGS)

	SAVE_CFLAGS="$CFLAGS"
	CFLAGS="$CFLAGS -Wno-error"
	AC_TRY_COMPILE([], [],
		WNOERROR_CFLAGS="-Wno-error",
		WNOERROR_CFLAGS="")
	AC_SUBST(WNOERROR_CFLAGS)
	CFLAGS="$SAVE_CFLAGS"
	unset SAVE_CFLAGS
	unset has_wnoerror

	dnl disable deprecated stuff
	dnl CFLAGS="$CFLAGS $DISABLE_DEPRECATED"
else
	AC_MSG_RESULT(no)
fi


dnl Check for -fno-strict-aliasing
FLAGS="-fno-strict-aliasing"
save_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS $FLAGS"
AC_MSG_CHECKING([whether [$]CC understands $FLAGS])
AC_TRY_COMPILE([], [], [compiler_has_option=yes], [compiler_has_option=no])
CFLAGS="$save_CFLAGS"
AC_MSG_RESULT($compiler_has_option)
if test $compiler_has_option = yes; then
	NO_STRICT_ALIASING_CFLAGS="$FLAGS"
fi
AC_SUBST([NO_STRICT_ALIASING_CFLAGS])

dnl Enable gtk-doc
GTK_DOC_CHECK(1.4)

dnl Enable yelp-tools
YELP_HELP_INIT

dnl ================================================================
dnl Plugins
dnl ================================================================

PLUGIN_LIBTOOL_FLAGS="-module -avoid-version"
AC_SUBST(PLUGIN_LIBTOOL_FLAGS)

PLUGINDIR='${libdir}/rhythmbox/plugins'
PLUGINDATADIR='${datadir}/rhythmbox/plugins'
SAMPLEPLUGINDIR='${libdir}/rhythmbox/sample-plugins'
AC_SUBST(PLUGINDIR)
AC_SUBST(PLUGINDATADIR)
AC_SUBST(SAMPLEPLUGINDIR)

dnl ================================================================
dnl Python plugins
dnl ================================================================

AC_MSG_CHECKING([whether Python plugin support is requested])
AC_ARG_ENABLE([python],
              AS_HELP_STRING([--enable-python[=@<:@no/auto/yes@:>@]],[Build with python support]),
              [enable_python=$enableval],
              [enable_python="auto"])
AC_MSG_RESULT([$enable_python])

if test "x$enable_python" != "xno"; then
	AM_PATH_PYTHON([3.2.3], [have_python=yes], [have_python=no])

	if test "x$have_python" = "xno" -a "x$enable_python" = "xyes"; then
		AC_MSG_ERROR([Python support explicitly requested, but no suitable Python interpreter could be found])
	fi

	PKG_CHECK_MODULES(PYTHON, [pygobject-3.0 >= $PYGOBJECT_REQUIRED],
			[have_pygobject=yes], [have_pygobject=no])

	if test "x$have_pygobject" = "xno" -a "x$enable_python" = "xyes"; then
		AC_MSG_ERROR([Python support explicitly requested, but pygobject-3.0 could not be found])
	fi

	if test "x$have_python" = "xyes" -a "x$have_pygobject" = "xyes"; then
		pyoverridesdir=`$PYTHON -c "import gi; print(gi._overridesdir)"`
		AC_SUBST(pyoverridesdir)
		build_python=yes
	fi
fi

AM_CONDITIONAL(ENABLE_PYTHON, test "x$build_python" = "xyes")

dnl ================================================================
dnl Vala plugins
dnl ================================================================
AC_MSG_CHECKING([whether Vala plugin support is requested])
AC_ARG_ENABLE([vala],
	AS_HELP_STRING([--enable-vala],[Enable Vala plugin support]),
	[enable_vala=$enableval have_vala=$enableval],
	[enable_vala=autodetect have_vala=yes])
AC_MSG_RESULT([$enable_vala])
with_vala=no
if test "x$enable_vala" = "xyes"; then
	AM_PROG_VALAC([$VALA_REQS])
	if test "x$VALAC" != "x"; then
		VAPIDIR=$datadir/vala/vapi
		# what?
		VAPIGEN=vapigen
		AC_SUBST([VALAC])
		AC_SUBST([VAPIDIR])
		AC_SUBST([VAPIGEN])
		with_vala=yes
	elif test "x$enable_vala" = "xyes"; then
		AC_MSG_ERROR([Vala plugin support explicitly requested, but not found])
	fi
fi
AM_CONDITIONAL(ENABLE_VALA, test "x$with_vala" = "xyes")

dnl ================================================================
dnl FM Radio Plugin
dnl ================================================================
AC_MSG_CHECKING([whether FM radio support is requested])
AC_ARG_ENABLE([fm-radio],
  AC_HELP_STRING([--disable-fm-radio],[Disable FM radio support]),,
  enable_fm_radio=yes)
if test "x$enable_fm_radio" != xno; then
  AC_CHECK_HEADER([linux/videodev2.h],,[enable_fm_radio=no])
fi
AM_CONDITIONAL(ENABLE_FM_RADIO, test "x$enable_fm_radio" != xno)


dnl check, for unit tests
PKG_CHECK_MODULES(CHECK, check, have_check=yes, have_check=no)
AM_CONDITIONAL([HAVE_CHECK],[test "x$have_check" = "xyes"])

dnl ================================================================
dnl Browser plugin
dnl ================================================================

AC_ARG_ENABLE([browser-plugin],
	[AS_HELP_STRING([--enable-browser-plugin],[compile the iTunes detection browser plugin])],
	[],[enable_browser_plugin=yes])

MOZILLA_PLUGINDIR=${MOZILLA_PLUGINDIR:-'${libdir}/mozilla/plugins'}
AC_ARG_VAR([MOZILLA_PLUGINDIR],[Where to install the browser plugin to])

AM_CONDITIONAL([ENABLE_BROWSER_PLUGIN], test x$enable_browser_plugin = xyes)
if test x$enable_browser_plugin = xyes; then
	ITMS_HANDLER="x-scheme-handler/itms;x-scheme-handler/itmss;"
fi
AC_SUBST(ITMS_HANDLER)

dnl ================================================================
dnl Use libdmapsharing for DAAP?
dnl ================================================================
AC_ARG_ENABLE(daap,
              AC_HELP_STRING([--disable-daap],
                            [Disable DAAP support]),,
              enable_daap=auto)
if test "x$enable_daap" != "xno"; then
	PKG_CHECK_MODULES(DMAPSHARING, libdmapsharing-3.0 >= 2.9.19,
			  have_libdmapsharing=yes,
			  have_libdmapsharing=no)
	if test "x$have_libdmapsharing" = "xno" -a "x$enable_daap" = "xyes"; then
		AC_MSG_ERROR([DAAP support explicitly requested, but libdmapsharing couldn't be found])
	fi
	if test x"$have_libdmapsharing" = "xyes"; then
		AC_DEFINE(HAVE_LIBDMAPSHARING, 1, [Define if libdmapsharing support is enabled])
	fi
fi

AM_CONDITIONAL(USE_LIBDMAPSHARING, test x"$have_libdmapsharing" = "xyes")

AC_SUBST(DMAPSHARING_CFLAGS)
AC_SUBST(DMAPSHARING_LIBS)

dnl ================================================================
dnl grilo plugin
dnl ================================================================
AC_ARG_ENABLE(grilo,
	      AC_HELP_STRING([--disable-grilo],
			     [Disable Grilo support]),,
	      enable_grilo=auto)
if test "x$enable_grilo" != "xno"; then
	PKG_CHECK_MODULES(GRILO, grilo-0.3 >= $GRILO_REQS, have_grilo=yes, have_grilo=no)
	if test "x$have_grilo" = "xno" -a "x$enable_grilo" = "xyes"; then
		AC_MSG_ERROR([Grilo support explicitly requested, but grilo couldn not be found])
	fi
	if test "x$have_grilo" = "xyes"; then
		AC_DEFINE(HAVE_GRILO, 1, [Define if Grilo support is enabled])
	fi
fi

AM_CONDITIONAL(ENABLE_GRILO, test x"$have_grilo" = "xyes")
AC_SUBST(GRILO_CFLAGS)
AC_SUBST(GRILO_LIBS)

dnl ================================================================
dnl end-game
dnl ================================================================

AC_OUTPUT([
rhythmbox.pc
Makefile
lib/Makefile
lib/libmediaplayerid/Makefile
metadata/Makefile
rhythmdb/Makefile
widgets/Makefile
podcast/Makefile
shell/Makefile
data/Makefile
data/rhythmbox.desktop.in
data/rhythmbox-device.desktop.in
data/ui/Makefile
data/icons/Makefile
data/icons/hicolor/Makefile
data/icons/hicolor/16x16/Makefile
data/icons/hicolor/16x16/apps/Makefile
data/icons/hicolor/16x16/status/Makefile
data/icons/hicolor/22x22/Makefile
data/icons/hicolor/22x22/apps/Makefile
data/icons/hicolor/24x24/Makefile
data/icons/hicolor/24x24/apps/Makefile
data/icons/hicolor/32x32/Makefile
data/icons/hicolor/32x32/apps/Makefile
data/icons/hicolor/48x48/Makefile
data/icons/hicolor/48x48/apps/Makefile
data/icons/hicolor/48x48/status/Makefile
data/icons/hicolor/256x256/Makefile
data/icons/hicolor/256x256/apps/Makefile
data/icons/hicolor/scalable/Makefile
data/icons/hicolor/scalable/apps/Makefile
data/icons/src/Makefile
sources/Makefile
sources/sync/Makefile
plugins/Makefile
plugins/android/Makefile
plugins/audiocd/Makefile
plugins/audioscrobbler/Makefile
plugins/brasero-disc-recorder/Makefile
plugins/daap/Makefile
plugins/fmradio/Makefile
plugins/im-status/Makefile
plugins/ipod/Makefile
plugins/mtpdevice/Makefile
plugins/iradio/Makefile
plugins/lirc/Makefile
plugins/lyrics/Makefile
plugins/pythonconsole/Makefile
plugins/artsearch/Makefile
plugins/magnatune/Makefile
plugins/generic-player/Makefile
plugins/rb/Makefile
plugins/rb/rbconfig.py
plugins/power-manager/Makefile
plugins/mmkeys/Makefile
plugins/context/Makefile
plugins/sendto/Makefile
plugins/replaygain/Makefile
plugins/mpris/Makefile
plugins/dbus-media-server/Makefile
plugins/rbzeitgeist/Makefile
plugins/notification/Makefile
plugins/grilo/Makefile
plugins/soundcloud/Makefile
plugins/webremote/Makefile
sample-plugins/Makefile
sample-plugins/sample/Makefile
sample-plugins/sample-python/Makefile
sample-plugins/sample-vala/Makefile
bindings/Makefile
bindings/vala/Makefile
bindings/gi/Makefile
help/Makefile
po/Makefile.in
tests/Makefile
doc/Makefile
doc/reference/Makefile
backends/Makefile
backends/gstreamer/Makefile
remote/Makefile
remote/dbus/Makefile
])

AC_MSG_NOTICE([Rhythmbox was configured with the following options:])
if test x"$with_database" = xtree; then
	AC_MSG_NOTICE([** Tree database is enabled])
else
	AC_MSG_ERROR([Unknown database!])
fi
if test x"${with_rhythmdb_debug}" != x0; then
	AC_MSG_NOTICE([** RhythmDB sanity checking enabled (may be slow!)])
fi


if test x"$enable_mmkeys" != "xyes"; then
	AC_MSG_NOTICE([   Multimedia keys support is disabled])
else
	AC_MSG_NOTICE([** Multimedia keys support is enabled])
fi

if test x"$use_ipod" = xyes; then
	AC_MSG_NOTICE([** iPod integration enabled])
else
	AC_MSG_NOTICE([   iPod integration disabled])
fi
if test x"$use_mtp" = xyes; then
	AC_MSG_NOTICE([** MTP integration enabled])
else
	AC_MSG_NOTICE([   MTP integration disabled])
fi
if test x"$have_libbrasero_media" != xno; then
	AC_MSG_NOTICE([** CD burning support enabled])
else
	AC_MSG_NOTICE([   CD burning support disabled])
fi
if test x"$have_libdmapsharing" = xyes; then
	AC_MSG_NOTICE([** DAAP (music sharing) support is enabled])
else
	AC_MSG_NOTICE([   DAAP (music sharing) support is disabled])
fi
if test x"$have_libnotify" = xyes; then
	AC_MSG_NOTICE([** libnotify support is enabled])
else
	AC_MSG_NOTICE([   libnotify support is disabled])
fi
if test x"$use_gudev" = xyes; then
	AC_MSG_NOTICE([** GUdev support enabled])
else
	AC_MSG_NOTICE([   GUdev support disabled])
fi
if test x"$build_python" = xyes; then
	AC_MSG_NOTICE([** Python plugin support enabled])
else
	AC_MSG_NOTICE([   Python plugin support disabled])
fi
if test x"$with_vala" = xyes; then
	AC_MSG_NOTICE([** Vala plugin support enabled])
else
	AC_MSG_NOTICE([   Vala plugin support disabled])
fi
if test x"$use_libsecret" = xyes; then
	AC_MSG_NOTICE([** Libsecret keyring support enabled])
else
	AC_MSG_NOTICE([   Libsecret keyring support disabled])
fi
if test "x$enable_fm_radio" != xno; then
	AC_MSG_NOTICE([** FM radio support enabled])
else
	AC_MSG_NOTICE([   FM radio support disabled])
fi

if test "x$enable_browser_plugin" != xno; then
	AC_MSG_NOTICE([** iTunes detection browser plugin (for podcasts) enabled])
else
	AC_MSG_NOTICE([   iTunes detection browser plugin (for podcasts) disabled])
fi

if test "x$enable_uninstalled" = xyes; then
	AC_MSG_NOTICE([** Uninstalled build enabled])
else
	AC_MSG_NOTICE([   Uninstalled build disabled])
fi

AC_MSG_NOTICE([End options])
