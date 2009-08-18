define(arch-tag)
# arch-tag: Toplevel Autoconf configuration script
AC_PREREQ(2.53)
AC_INIT([rhythmbox],
	[0.12.3],
	[http://bugzilla.gnome.org/enter_bug.cgi?product=rhythmbox])
AC_CONFIG_MACRO_DIR(macros)
AM_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE([1.9 dist-bzip2 no-dist-gzip])

AC_SUBST(ACLOCAL_AMFLAGS, "-I macros")

AM_MAINTAINER_MODE

m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])],)

dnl XXXX hack to kill off all the libtool tags ...
dnl it isn't like we are using C++ or Fortran.
m4_define([_LT_AC_TAGCONFIG],[])


IT_PROG_INTLTOOL([0.35.0])

AC_ISC_POSIX
AC_PROG_CC
AC_PROG_CXX
AC_STDC_HEADERS
AM_PROG_LIBTOOL
m4_ifdef([LT_OUTPUT], [LT_OUTPUT])
AC_C_BIGENDIAN
AC_CHECK_SIZEOF(long)

DBUS_MIN_REQS=0.35
GST_0_10_REQS=0.10.20
GTK_REQS=2.12.0
GLIB_REQS=2.16.0
GNOME_MEDIA_PROFILES_REQS=2.8
LIBNOTIFY_REQS=0.4.1
LIBGPOD_REQS=0.6
MUSICBRAINZ_REQS=2.1.0
MUSICBRAINZ3_REQS=3.0.2
NCB_MIN_REQS=2.21.6
BRASERO_MIN_REQS=0.9.1
TOTEM_PLPARSER_REQS=2.26.0
VALA_REQS=0.1.0
AVAHI_REQS=0.6
LIBSOUP_REQS=2.26.0
GUDEV_REQS=143
LIBMTP_REQS=0.3.0

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

mkdtemp_missing=false
AC_CHECK_FUNC(mkdtemp,
    [AC_DEFINE([HAVE_MKDTEMP], 1, [Have GlibC function to make temp dirs])],
    mkdtemp_missing=true)
AM_CONDITIONAL(MKDTEMP_MISSING, test x$mkdtemp_missing = xtrue)

PKG_PROG_PKG_CONFIG

PKG_CHECK_MODULES(RB_CLIENT, glib-2.0 >= $GLIB_REQS gio-2.0 >= $GLIB_REQS)

# libgnomeui still required with gtk < 2.14
PKG_CHECK_EXISTS(gtk+-2.0 >= 2.14, [LIBGNOME_REQS=], [LIBGNOME_REQS=libgnomeui-2.0])

# use gtk status icon with gtk >= 2.16
PKG_CHECK_EXISTS(gtk+-2.0 >= 2.16, [
	use_gtk_status_icon=yes
	AC_DEFINE(USE_GTK_STATUS_ICON, 1, [Use GtkStatusIcon])
], [use_gtk_status_icon=no])
AM_CONDITIONAL(USE_GTK_STATUS_ICON, test x"$use_gtk_status_icon" = xyes)


dnl  note: gio-unix-2.0 is here for libmediaplayerid
PKG_CHECK_MODULES(RHYTHMBOX,				\
		  gtk+-2.0 >= $GTK_REQS			\
		  glib-2.0 >= $GLIB_REQS		\
		  gio-2.0 >= $GLIB_REQS			\
		  gio-unix-2.0 >= $GLIB_REQS		\
		  gnome-media-profiles >= $GNOME_MEDIA_PROFILES_REQS \
		  libsoup-2.4 >= $LIBSOUP_REQS		\
		  libsoup-gnome-2.4 >= $LIBSOUP_REQS	\
		  $LIBGNOME_REQS)

PKG_CHECK_MODULES(TOTEM_PLPARSER, totem-plparser >= $TOTEM_PLPARSER_REQS, have_totem_plparser=yes, have_totem_plparser=no)
if test x$have_totem_plparser != xyes; then
   AC_MSG_ERROR([totem playlist parsing library not found or too old])
else
   if test "x`pkg-config --variable uselibcamel totem-plparser`" = "xno" ; then
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
	  AC_DEFINE(HAVE_GUDEV, 1, [something])
	else
	  use_gudev=no
	fi
else
  use_gudev=no
fi
AM_CONDITIONAL(USE_GUDEV, test x"$use_gudev" = xyes)

dnl hal remnants
AC_ARG_WITH(hal,
	      AC_HELP_STRING([--without-hal],
			     [Disable HAL support]))
if test "x$with_hal" != "xno"; then
  PKG_CHECK_MODULES(HAL, hal >= 0.5 hal < 0.6, enable_hal=yes, enable_hal=no)
  if test "x$enable_hal" != "xyes" -a "x$with_hal" = "xyes"; then
      AC_MSG_ERROR([HAL support explicitly requested but HAL couldn't be found])
  fi

  if test "x$enable_hal" = "xyes"; then
  	AC_DEFINE(HAVE_HAL, 1, [Define if you have HAL support])
	AC_SUBST(HAL_CFLAGS)
    AC_SUBST(HAL_LIBS)
  fi	
fi
AM_CONDITIONAL(HAVE_HAL, test x"$enable_hal" = xyes)


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
	if test x"$use_gudev" = "xyes" -o x"$enable_hal" = "xyes"; then
	  can_use_mtp=yes
	fi

	PKG_CHECK_MODULES(MTP, libmtp >= $LIBMTP_REQS, have_libmtp=yes, have_libmtp=no)
	if test x"$with_mtp" = "xyes"; then
	  if test x"$have_libmtp" = "xno"; then
	    AC_MSG_ERROR([MTP explicitly requested but libmtp is not available])
	  fi
	  if test x"$can_use_mtp" = "xno"; then
	    AC_MSG_ERROR([MTP explicitly requested but GUdev and HAL are not available])
	  fi
	fi
	if test "x$have_libmtp" = "xyes" -a "x$can_use_mtp" = "xyes"; then
	  use_mtp=yes
	  AC_SUBST(MTP_CFLAGS)
	  AC_SUBST(MTP_LIBS)
	fi
fi
AM_CONDITIONAL(USE_MTP, test x"$use_mtp" = xyes)


dnl gnome-keyring support

AC_ARG_WITH(gnome-keyring,
            AC_HELP_STRING([--with-gnome-keyring],
			   [Enable gnome-keyring support]),,
	      with_gnome_keyring=auto)
if test "x$with_gnome_keyring" != "xno"; then

	PKG_CHECK_MODULES(GNOME_KEYRING, gnome-keyring-1, have_gnome_keyring=yes, have_gnome_keyring=no)
	if test "x$have_gnome_keyring" = "xno" -a "x$with_gnome_keyring" = "xyes"; then
	  AC_MSG_ERROR([gnome-keyring support explicitly requested but gnome-keyring couldn't be found])
	fi
	if test "x$have_gnome_keyring" = "xyes"; then
	   AC_DEFINE(WITH_GNOME_KEYRING, 1, [Define if gnome-keyring support is enabled])
	   use_gnome_keyring=yes
	  AC_SUBST(GNOME_KEYRING_CFLAGS)
	  AC_SUBST(GNOME_KEYRING_LIBS)
	fi
fi
AM_CONDITIONAL(USE_GNOME_KEYRING, test x"$use_gnome_keyring" = xyes)

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
PKG_CHECK_MODULES(GSTREAMER_0_10, \
	gstreamer-0.10 >= $GST_0_10_REQS
	gstreamer-base-0.10 >= $GST_0_10_REQS
	gstreamer-plugins-base-0.10 >= $GST_0_10_REQS)

RHYTHMBOX_CFLAGS="$RHYTHMBOX_CFLAGS $GSTREAMER_0_10_CFLAGS"
RHYTHMBOX_LIBS="$RHYTHMBOX_LIBS $GSTREAMER_0_10_LIBS"

dnl DAAP (iTunes Music Shares)
AC_ARG_ENABLE(daap,
	      AC_HELP_STRING([--disable-daap],
			     [Disable Digital Audio Access Protocol (music sharing) in rhythmbox]))

AC_ARG_WITH(mdns,
   AC_HELP_STRING([--with-mdns=auto|avahi],
   [Select the mDNS/DNS-SD implementation to use (default auto)]),,
   with_mdns=auto)

have_avahi=no
have_mdns=no
use_avahi=no

PKG_CHECK_MODULES(AVAHI,
   avahi-client >= $AVAHI_REQS
   avahi-glib >= $AVAHI_REQS,
   have_avahi=yes,
   have_avahi=no)

if test x"$with_mdns" = xauto || test x"$with_mdns" = xavahi; then
   if test x"$have_avahi" = xyes; then
      MDNS_CFLAGS=$AVAHI_CFLAGS
      MDNS_LIBS=$AVAHI_LIBS
      AC_DEFINE(WITH_AVAHI, 1, [Define if mDNS/DNS-SD implementation uses Avahi])
      use_avahi=yes
      AC_MSG_NOTICE([Detected Avahi, using it for mDNS/DNS-SD])
      have_mdns=yes
   elif test x"$with_mdns" = xavahi; then
      AC_MSG_ERROR([Avahi explicitly requested but not found.])
   fi
fi

AM_CONDITIONAL(USE_AVAHI, test "x$use_avahi" = "xyes")

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
    libnotify_version=`pkg-config --modversion libnotify`
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

dnl daap support
if test "x$enable_daap" != "xno"; then
	if test x"$have_mdns" = xno; then
		if test "x$enable_daap" = "xyes"; then
			AC_MSG_ERROR([DAAP support explicitly requested, but no mDNS implementation found.  Install Avahi])
		fi
		enable_daap=no
	else
		AC_DEFINE(WITH_DAAP_SUPPORT, 1, [Define if DAAP should be enabled])
		enable_daap="yes"
		AC_SUBST(MDNS_CFLAGS)
		AC_SUBST(MDNS_LIBS)
	fi
fi
AM_CONDITIONAL(USE_DAAP, test "x$enable_daap" != "xno")

AC_CHECK_LIB(z, uncompress)

dnl check for libgstcdda, needed to list the audio tracks
PKG_CHECK_MODULES(GSTCDDA, gstreamer-cdda-0.10)
AC_SUBST(GSTCDDA_LIBS)
AC_SUBST(GSTCDDA_CFLAGS)

dnl check for MusicBrainz
have_sj_metadata_getter=no
AC_ARG_ENABLE(musicbrainz, AC_HELP_STRING([--disable-musicbrainz],
				[don't build with MusicBrainz support]))
if test x"$enable_musicbrainz" != "xno"; then
	PKG_CHECK_MODULES(MUSICBRAINZ3, libmusicbrainz3 >= $MUSICBRAINZ3_REQS, [have_musicbrainz3=yes], [have_musicbrainz3=no])
	PKG_CHECK_MODULES(MUSICBRAINZ, libmusicbrainz >= $MUSICBRAINZ_REQS, [have_musicbrainz=yes], [have_musicbrainz=no])

	AC_SUBST(MUSICBRAINZ3_CFLAGS)
	AC_SUBST(MUSICBRAINZ3_LIBS)
	AC_SUBST(MUSICBRAINZ_CFLAGS)
	AC_SUBST(MUSICBRAINZ_LIBS)

	if test x"$have_musicbrainz3" = xyes; then
		AC_DEFINE([HAVE_MUSICBRAINZ3], 1, [Whether libmusicbrainz3 is available])
	fi
	if test x"$have_musicbrainz" = xyes; then
		AC_DEFINE([HAVE_MUSICBRAINZ], 1, [define if you have Musicbrainz])
	fi

	if test x"$have_musicbrainz3" = "xyes" || test x"$have_musicbrainz" = "xyes"; then
		have_sj_metadata_getter=yes
		AC_DEFINE([HAVE_SJ_METADATA_GETTER], 1, [Whether to use the sound-juicer metadata getter code])
	else
		if test x"$enable_musicbrainz" = xyes; then
			AC_MSG_ERROR([MusicBrainz requested, but neither libmusicbrainz nor libmusicbrainz3 are available])
		fi
	fi
fi
AM_CONDITIONAL([HAVE_MUSICBRAINZ], [test "x$have_musicbrainz" = "xyes"])
AM_CONDITIONAL([HAVE_MUSICBRAINZ3], [test "x$have_musicbrainz3" = "xyes"])
AM_CONDITIONAL([HAVE_SJ_METADATA_GETTER], [test "x$have_sj_metadata_getter" = "xyes"])

AC_PATH_XTRA
CFLAGS="$CFLAGS $X_CFLAGS"
#LIBS=$X_LIBS

dnl Multimedia keys
have_xfree=no
AC_COMPILE_IFELSE([
	#include <X11/XF86keysym.h>
int main(int argc,char **argv) {
  return 0;
}
], have_xfree=yes)
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
AC_ARG_WITH(libbrasero-media,
	    AC_HELP_STRING([--with-libbrasero-media],
	    		   [Build with libbrasero-media support]),,
	    with_libbrasero_media=auto)
if test x"$with_libbrasero_media" != "xno"; then
    PKG_CHECK_MODULES(LIBBRASERO_MEDIA, [libbrasero-media >= $BRASERO_MIN_REQS] gtk+-x11-2.0, have_libbrasero_media=yes, have_libbrasero_media=no)
fi
if test "x$have_libbrasero_media" = "xyes"; then
    AC_DEFINE([HAVE_LIBBRASERO_MEDIA], 1, [Have libbrasero-media])
fi
AM_CONDITIONAL(HAVE_LIBBRASERO_MEDIA, test x$have_libbrasero_media = xyes)
AC_SUBST(HAVE_LIBBRASERO_MEDIA)

dnl libnautilus-burn support
have_libnautilus_burn=no
AC_ARG_WITH(libnautilus-burn,
	    AC_HELP_STRING([--with-libnautilus-burn],
	    		   [Build with libnautilus-burn support]),,
	    with_libnautilus_burn=auto)
if test x"$have_libbrasero_media" = "xno" -a x"$with_libnautilus_burn" != "xno"; then
    PKG_CHECK_MODULES(LIBNAUTILUS_BURN, [libnautilus-burn >= $NCB_MIN_REQS], have_libnautilus_burn=yes, have_libnautilus_burn=no)
fi
if test "x$have_libnautilus_burn" = "xyes"; then
    AC_DEFINE([HAVE_NAUTILUS_BURN], 1, [Have nautilus-burn])
fi
AM_CONDITIONAL(HAVE_NAUTILUS_BURN, test x$have_libnautilus_burn = xyes)
AC_SUBST(HAVE_NAUTILUS_BURN)


AC_SUBST(CFLAGS)
AC_SUBST(LDFLAGS)

AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)
AC_PATH_PROG(GDK_PIXBUF_CSOURCE, gdk-pixbuf-csource)
AC_PATH_PROG(GCONFTOOL, gconftool-2)

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

GETTEXT_PACKAGE=rhythmbox
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [Define to the Gettext package name])

AM_GLIB_GNU_GETTEXT

dnl Workaround for automake 1.8
AC_SUBST(mkdir_p) if test x"$mkdir_p" = "x"; then
  MKINSTALLDIRS="mkinstalldirs"
fi
AC_SUBST(MKINSTALLDIRS)

dnl DBUS
PKG_CHECK_MODULES(DBUS, dbus-glib-1 >= $DBUS_MIN_REQS)

DBUS_CFLAGS="$DBUS_CFLAGS -DDBUS_API_SUBJECT_TO_CHANGE"
DBUS_GLIB_BIN="`$PKG_CONFIG --variable=exec_prefix dbus-glib-1`/bin"
AC_SUBST(DBUS_GLIB_BIN)

AM_GCONF_SOURCE_2

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
	      enable_uninstalled=yes,)
if test x"$enable_uninstalled" = xyes; then
	AC_DEFINE(USE_UNINSTALLED_DIRS, 1, [Define to look for files in source tree locations])
fi
ROOT_UNINSTALLED_DIR="`pwd`"
AC_DEFINE_UNQUOTED(SHARE_UNINSTALLED_DIR,"`pwd`/$srcdir/data",[path to source data dir])
AC_DEFINE_UNQUOTED(SHARE_UNINSTALLED_BUILDDIR,"`pwd`/data",[path to built source data dir])
AC_DEFINE_UNQUOTED(METADATA_UNINSTALLED_DIR,"`pwd`/metadata",[path to metadata build dir])
AC_SUBST(ROOT_UNINSTALLED_DIR)

dnl libsexy is only required with gtk < 2.16
PKG_CHECK_EXISTS(gtk+-2.0 >= 2.16, [need_libsexy=no], [need_libsexy=yes])

AC_ARG_WITH(internal-libsexy,
            AC_HELP_STRING([--with-internal-libsexy],
			   [Build using internal libsexy library]),,
	      with_internal_libsexy=no)
if test "x$need_libsexy" = "xyes" && test "x$with_internal_libsexy" = "xno"; then
	PKG_CHECK_MODULES(LIBSEXY, libsexy >= 0.1.5,
		          with_internal_libsexy=no,
		          with_internal_libsexy=yes)
fi
AC_SUBST(LIBSEXY_CFLAGS)
AC_SUBST(LIBSEXY_LIBS)
AM_CONDITIONAL(WITH_INTERNAL_LIBSEXY, test "x$with_internal_libsexy" = "xyes")


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
	-Wcast-align -Wall \
	-Werror -std=gnu89"

	if echo "$CFLAGS" | grep -e '-O[1-9]'; then
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

dnl Enable gnome-doc-utils
GNOME_DOC_INIT


dnl ================================================================
dnl Plugins
dnl ================================================================

PLUGIN_LIBTOOL_FLAGS="-module -avoid-version"
AC_SUBST(PLUGIN_LIBTOOL_FLAGS)

PLUGINDIR='${libdir}/rhythmbox/plugins'
AC_SUBST(PLUGINDIR)

dnl ================================================================
dnl Python plugins
dnl ================================================================

AC_MSG_CHECKING([whether Python plugin support is requested])
AC_ARG_ENABLE([python],
	AS_HELP_STRING([--enable-python],[Enable python support]),
	[enable_python=$enableval have_python=$enableval],
	[enable_python=autodetect have_python=yes])
AC_MSG_RESULT([$enable_python])

if test "x$have_python" != "xyes"; then
	if test "x$enable_python" = "xyes"; then
		AC_MSG_ERROR([Python not found])
	elif test "x$enable_python" = "xautodetect"; then
		enable_python=no
		AC_MSG_WARN([Python not found, disabling python support])
	fi
fi

if test "x$have_python" != "xno"; then
	AM_PATH_PYTHON([2.3],[],[have_python=no])
	if test "x$PYTHON" = "x:"; then
		have_python=no
	fi
fi

if test "x$have_python" != "xno"; then
	PY_PREFIX=`$PYTHON -c 'import sys ; print sys.prefix'`
	PY_EXEC_PREFIX=`$PYTHON -c 'import sys ; print sys.exec_prefix'`
	PYTHON_LIBS="-lpython$PYTHON_VERSION"
	if test -d $PY_EXEC_PREFIX/lib64/python$PYTHON_VERSION ; then
		PYTHON_LIB_LOC="-L$PY_EXEC_PREFIX/lib64/python$PYTHON_VERSION/config"
		PYTHON_MAKEFILE="$PY_EXEC_PREFIX/lib64/python$PYTHON_VERSION/config/Makefile"
	else
		PYTHON_LIB_LOC="-L$PY_EXEC_PREFIX/lib/python$PYTHON_VERSION/config"
		PYTHON_MAKEFILE="$PY_EXEC_PREFIX/lib/python$PYTHON_VERSION/config/Makefile"
	fi
	PYTHON_CFLAGS="-I$PY_PREFIX/include/python$PYTHON_VERSION"
	PYTHON_LOCALMODLIBS=`sed -n -e 's/^LOCALMODLIBS=\(.*\)/\1/p' $PYTHON_MAKEFILE`
	PYTHON_BASEMODLIBS=`sed -n -e 's/^BASEMODLIBS=\(.*\)/\1/p' $PYTHON_MAKEFILE`
	PYTHON_OTHER_LIBS=`sed -n -e 's/^LIBS=\(.*\)/\1/p' $PYTHON_MAKEFILE`
	PYTHON_EXTRA_LIBS="$PYTHON_LOCALMODLIBS $PYTHON_BASEMODLIBS $PYTHON_OTHER_LIBS"
	AC_SUBST([PYTHON_LIBS])
	AC_SUBST([PYTHON_LIB_LOC])
	AC_SUBST([PYTHON_CFLAGS])
	AC_SUBST([PYTHON_EXTRA_LIBS])

	PYGTK_REQUIRED=2.8.0
	GST_PYTHON_REQUIRED=0.10.1

	PKG_CHECK_MODULES([PYGTK], [
		pygtk-2.0 >= $PYGTK_REQUIRED] \
		gst-python-0.10 >= $GST_PYTHON_REQUIRED,
		[],
		[
		have_python=no
		if test "x$enable_python" = "xyes"; then
			AC_MSG_ERROR([$PYGTK_PKG_ERRORS])
		elif test "x$enable_python" = "xautodetect"; then
			enable_python=no
			AC_MSG_WARN([$PYGTK_PKG_ERRORS])
			AC_MSG_WARN([Disabling python support])
		fi
		])

	AC_SUBST([PYGTK_CFLAGS])
	AC_SUBST([PYGTK_LIBS])
fi

if test "x$have_python" != "xno"; then
	AC_MSG_CHECKING([for pygtk defs])
	PYGTK_DEFSDIR=`$PKG_CONFIG --variable=defsdir pygtk-2.0`
	AC_MSG_RESULT([$PYGTK_DEFSDIR])

	AC_MSG_CHECKING([for gst-python defs])
	GST_PYTHON_DEFSDIR=`$PKG_CONFIG --variable=defsdir gst-python-0.10`
	AC_MSG_RESULT([$GST_PYTHON_DEFSDIR])

	AC_MSG_CHECKING([for pygtk codegen])
	PYGTK_CODEGEN="$PYTHON `$PKG_CONFIG --variable=codegendir pygtk-2.0`/codegen.py"
	AC_MSG_RESULT([$PYGTK_CODEGEN])

	AC_MSG_CHECKING([for pygtk h2def])
	PYGTK_H2DEF="$PYTHON `$PKG_CONFIG --variable=codegendir pygtk-2.0`/h2def.py"
	AC_MSG_RESULT([$PYGTK_H2DEF])

	AC_SUBST([PYGTK_DEFSDIR])
	AC_SUBST([GST_PYTHON_DEFSDIR])
	AC_SUBST([PYGTK_CODEGEN])
	AC_SUBST([PYGTK_H2DEF])

	PKG_CHECK_EXISTS([pygobject-2.0 >= 2.13.2],
			 AC_DEFINE([PYGOBJECT_CAN_MARSHAL_GVALUE], [1], [Define if pygobject can marshal GValues]))
fi

if test "x$have_python" != "xno" -a "x$enable_python" != "xno"; then
	enable_python=yes
	AC_DEFINE([ENABLE_PYTHON],[1],[Define to compile with python plugin support])
fi

AM_CONDITIONAL([ENABLE_PYTHON],[test "x$enable_python" = "xyes"])

dnl ================================================================
dnl Vala plugins
dnl ================================================================
AC_MSG_CHECKING([whether Vala plugin support is requested])
AC_ARG_ENABLE([vala],
	AS_HELP_STRING([--enable-vala],[Enable Vala plugin support]),
	[enable_vala=$enableval have_vala=$enableval],
	[enable_vala=autodetect have_vala=yes])
AC_MSG_RESULT([$enable_vala])
if test "x$enable_vala" = "xyes"; then
	PKG_CHECK_MODULES(VALA, vala-1.0 >= $VALA_REQS, with_vala=yes,
		          with_vala=no)
	if test "x$with_vala" = "xyes"; then
		VALAC="`pkg-config --variable=prefix vala-1.0`/bin/valac"
		AC_SUBST([VALAC])
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

MOZILLA_PLUGINDIR="${MOZILLA_PLUGINDIR:="\${libdir}/mozilla/plugins"}"
AC_ARG_VAR([MOZILLA_PLUGINDIR],[Where to install the browser plugin to])

AM_CONDITIONAL([ENABLE_BROWSER_PLUGIN], test x$enable_browser_plugin = xyes)

dnl ================================================================
dnl end-game
dnl ================================================================

AC_OUTPUT([
Makefile
macros/Makefile
lib/Makefile
lib/libmediaplayerid/Makefile
metadata/Makefile
rhythmdb/Makefile
widgets/Makefile
widgets/libsexy/Makefile
podcast/Makefile
shell/Makefile
data/Makefile
data/rhythmbox.desktop.in
data/ui/Makefile
data/icons/Makefile
data/icons/hicolor/Makefile
data/icons/hicolor/16x16/Makefile
data/icons/hicolor/16x16/actions/Makefile
data/icons/hicolor/16x16/apps/Makefile
data/icons/hicolor/16x16/places/Makefile
data/icons/hicolor/16x16/status/Makefile
data/icons/hicolor/22x22/Makefile
data/icons/hicolor/22x22/actions/Makefile
data/icons/hicolor/22x22/apps/Makefile
data/icons/hicolor/22x22/places/Makefile
data/icons/hicolor/22x22/status/Makefile
data/icons/hicolor/24x24/Makefile
data/icons/hicolor/24x24/apps/Makefile
data/icons/hicolor/32x32/Makefile
data/icons/hicolor/32x32/actions/Makefile
data/icons/hicolor/32x32/apps/Makefile
data/icons/hicolor/32x32/places/Makefile
data/icons/hicolor/32x32/status/Makefile
data/icons/hicolor/48x48/Makefile
data/icons/hicolor/48x48/apps/Makefile
data/icons/hicolor/scalable/Makefile
data/icons/hicolor/scalable/actions/Makefile
data/icons/hicolor/scalable/apps/Makefile
data/icons/hicolor/scalable/places/Makefile
data/icons/hicolor/scalable/status/Makefile
sources/Makefile
plugins/Makefile
plugins/sample/Makefile
plugins/audiocd/Makefile
plugins/coherence/Makefile
plugins/coherence/upnp_coherence/Makefile
plugins/audioscrobbler/Makefile
plugins/cd-recorder/Makefile
plugins/brasero-disc-recorder/Makefile
plugins/daap/Makefile
plugins/fmradio/Makefile
plugins/im-status/Makefile
plugins/ipod/Makefile
plugins/mtpdevice/Makefile
plugins/iradio/Makefile
plugins/lirc/Makefile
plugins/lyrics/Makefile
plugins/lyrics/lyrics/Makefile
plugins/sample-python/Makefile
plugins/sample-vala/Makefile
plugins/pythonconsole/Makefile
plugins/artdisplay/Makefile
plugins/artdisplay/artdisplay/Makefile
plugins/magnatune/Makefile
plugins/magnatune/magnatune/Makefile
plugins/jamendo/Makefile
plugins/jamendo/jamendo/Makefile
plugins/generic-player/Makefile
plugins/rb/Makefile
plugins/power-manager/Makefile
plugins/status-icon/Makefile
plugins/visualizer/Makefile
plugins/mmkeys/Makefile
bindings/Makefile
bindings/python/Makefile
bindings/vala/Makefile
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

if test x"$have_musicbrainz3" = "xyes" && test x"$have_musicbrainz" = "xyes"; then
	AC_MSG_NOTICE([** MusicBrainz support is enabled (using libmusicbrainz3 and libmusicbrainz)])
elif test x"$have_musicbrainz3" = "xyes"; then
	AC_MSG_NOTICE([** MusicBrainz support is enabled (using libmusicbrainz3)])
elif test x"$have_musicbrainz" = "xyes"; then
	AC_MSG_NOTICE([** MusicBrainz support is enabled (using libmusicbrainz)])
else
	AC_MSG_NOTICE([   MusicBrainz support is disabled])
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
	AC_MSG_NOTICE([** CD burning support enabled (using Brasero)])
elif test x"$have_libnautilus_burn" != xno; then
	AC_MSG_NOTICE([** CD burning support enabled (using nautilus-cd-burner)])
else
	AC_MSG_NOTICE([   CD burning support disabled])
fi
if test x"$enable_daap" = xyes; then
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
elif test x"$enable_hal" = xyes; then
	AC_MSG_NOTICE([** HAL support enabled])
else
	AC_MSG_NOTICE([   GUdev support disabled])
fi
if test x"$enable_python" = xyes; then
	AC_MSG_NOTICE([** Python plugin support enabled])
else
	AC_MSG_NOTICE([   Python plugin support disabled])
fi
if test x"$with_vala" = xyes; then
	AC_MSG_NOTICE([** Vala plugin support enabled])
else
	AC_MSG_NOTICE([   Vala plugin support disabled])
fi
if test x"$use_gnome_keyring" = xyes; then
	AC_MSG_NOTICE([** gnome-keyring support enabled])
else
	AC_MSG_NOTICE([   gnome-keyring support disabled])
fi
if test x"$need_libsexy" = xyes; then
	if test x"$with_internal_libsexy" = xyes; then
		AC_MSG_NOTICE([   using internal libsexy])
	else
		AC_MSG_NOTICE([** using system-wide libsexy])
	fi
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

AC_MSG_NOTICE([End options])
