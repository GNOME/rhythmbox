define(arch-tag)
# arch-tag: Toplevel Autoconf configuration script
AC_PREREQ(2.53)
AC_INIT(shell)
AM_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE(rhythmbox, 0.8.99)

AM_MAINTAINER_MODE

AC_PROG_INTLTOOL

AC_ISC_POSIX
AC_PROG_CC
AC_STDC_HEADERS
AM_PROG_LIBTOOL
AC_C_BIGENDIAN
AC_CHECK_SIZEOF(long)

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

dnl iPod support
dnl gnome-vfs 2.7.4 is required because the gnome_vfs_volume_get_hal_udi 
dnl function was added in that release.
dnl enable_ipod = "no"

HAL_REQUIRED=0.2.92

AC_ARG_ENABLE(ipod,
	      AC_HELP_STRING([--enable-ipod],
			     [Enable iPod support in Rhythmbox]))
AM_CONDITIONAL(USE_IPOD, test x"$enable_ipod" = xyes)
if test x"$enable_ipod" = xyes; then
	AC_DEFINE(WITH_IPOD_SUPPORT, 1, [Define if iPod support is enabled])
	USE_HAL=""
	PKG_CHECK_MODULES(HAL, hal gnome-vfs-2.0 >= 2.7.4,
		[ AC_DEFINE([HAVE_HAL], 1, [Use the HAL library]) ]
		[ USE_HAL="dbus-glib-1 hal >= $HAL_REQUIRED" ], :)
fi


PKG_CHECK_MODULES(RHYTHMBOX,                    \
                  gtk+-2.0 >= 2.4               \
                  libgnomeui-2.0                \
		  libglade-2.0                  \
		  gnome-vfs-2.0 >= 2.6          \
		  gnome-vfs-module-2.0          \
		  $USE_HAL)

RHYTHMBOX_CFLAGS="$CFLAGS $RHYTHMBOX_CFLAGS"


dnl Database
AC_ARG_WITH(database,
              AC_HELP_STRING([--with-database=tree|libgda],
			     [Select the database to use (default tree)]),,
	      with_database=tree)
AM_CONDITIONAL(USE_TREEDB, test x"$with_database" = xtree)
AM_CONDITIONAL(USE_GDADB, test x"$with_database" = xlibgda)

GDA_CFLAGS=""
GDA_LIBS=""
case "x$with_database" in
  "xtree")
    AC_DEFINE(WITH_RHYTHMDB_TREE, 1, [Define if you are using the RhythmDB tree database])
    ;;
  "xlibgda")
    AC_DEFINE(WITH_RHYTHMDB_GDA, 1, [Define if you are using the RhythmDB sqlite/libgda database])
    dnl FIXME: check for sqlite, too?
    PKG_CHECK_MODULES(GDA, libgda > 1.0.3)
    ;;
  *)
    AC_MSG_ERROR([Unknown database selected])
    ;;
esac
AC_SUBST(GDA_CFLAGS)
AC_SUBST(GDA_LIBS)


dnl Database debugging
AC_ARG_WITH(rhythmdb-debug,
              AC_HELP_STRING([--with-rhythmdb-debug=0|1|2],
			     [Level of RhythmDB sanity checking]),,with_rhythmdb_debug=0)
if test x"${with_rhythmdb_debug}" != x0; then
   AC_DEFINE_UNQUOTED([RHYTHMDB_ENABLE_SANITY_CHECK], "${with_rhythmdb_debug}", [Define to the level of RhythmDB sanity checking])
fi

dnl dashboard support
AC_ARG_ENABLE(dashboard,
	      [  --enable-dashboard	  Enable dashboard support (http://www.nat.org/dashboard) (default disabled)],enable_dashboard=yes,enable_dashboard=no)
if test "x$enable_dashboard" = "xyes" ; then
   AC_DEFINE([WITH_DASHBOARD], 1, [Define to enable support for dashboard (http://www.nat.org/dashboard)])
fi
AM_CONDITIONAL(WITH_DASHBOARD, test x"$enable_dashboard" = xyes)

dnl Sound system
AC_ARG_WITH(player,
            AC_HELP_STRING([--with-player=gstreamer|xine],[Select the player to use]),,
	    with_player=gstreamer)
if test "x$with_player" = "xxine"; then
	SOUNDSYSTEM_DEPS="libxine >= 1.0.0"
	AC_SUBST(SOUNDSYSTEM_DEPS)

	PKG_CHECK_MODULES(SOUNDSYSTEM, $SOUNDSYSTEM_DEPS)

	AC_DEFINE(HAVE_XINE,1,[define if you want to use the Xine libraries])

	RHYTHMBOX_CFLAGS="$RHYTHMBOX_CFLAGS $SOUNDSYSTEM_CFLAGS"
	RHYTHMBOX_LIBS="$RHYTHMBOX_LIBS $SOUNDSYSTEM_LIBS"

	dnl Remove when xine-lib is newer than rc0
	SAVE_CFLAGS="$CFLAGS"
	CFLAGS="$RHYTHMBOX_CFLAGS"
	SAVE_LIBS="$LIBS"
	LIBS="$RHYTHMBOX_LIBS"
	AC_MSG_CHECKING(if XINE_PARAM_AUDIO_CLOSE_DEVICE is supported)
	AC_TRY_COMPILE([
		#include <xine.h>
		],[
		int arg = 0, foo;
		foo = XINE_PARAM_AUDIO_CLOSE_DEVICE;
		],[AC_MSG_RESULT(yes)
		(AC_DEFINE(HAVE_XINE_CLOSE,1,[Define if you have the audio device close feature in xine-lib]))], AC_MSG_RESULT(no))
	CFLAGS="$SAVE_CFLAGS"
	LIBS="$SAVE_LIBS"

	dnl Remove when xine-lib is newer than rc2
	SAVE_CFLAGS="$CFLAGS"
	CFLAGS="$RHYTHMBOX_CFLAGS -Werror"
	SAVE_LIBS="$LIBS"
	LIBS="$RHYTHMBOX_LIBS"
	AC_CHECK_LIB(xine, xine_osd_show_unscaled,
			[AC_CHECK_HEADER(xine.h,
			  [AC_DEFINE(HAVE_NULL_VIDEO,1,[Define if you have the NULL video feature in xine-lib])])])
	CFLAGS="$SAVE_CFLAGS"
	LIBS="$SAVE_LIBS"
else if test "x$with_player" = "xgstreamer"; then
	dnl Now we're ready to ask for gstreamer libs and cflags
	dnl And we can also ask for the right version of gstreamer
	HAVE_SOUNDSYSTEM=no

	GST_MAJORMINOR=0.8
	GST_REQUIRED_VERSION=0.8.1
	PKG_CHECK_MODULES(SOUNDSYSTEM, \
		gstreamer-$GST_MAJORMINOR >= $GST_REQUIRED_VERSION gstreamer-libs-$GST_MAJORMINOR >= $GST_REQUIRED_VERSION
		gstreamer-control-$GST_MAJORMINOR >= $GST_REQUIRED_VERSION gstreamer-gconf-$GST_MAJORMINOR >= $GST_REQUIRED_VERSION,
		HAVE_SOUNDSYSTEM=yes,HAVE_SOUNDSYSTEM=no)

	dnl Give error and exit if we don't have gstreamer
	if test "x$HAVE_SOUNDSYSTEM" = "xno"; then
		AC_MSG_ERROR([GStreamer not found, or older than version $GST_REQUIRED_VERSION])
	fi
	have_gstreamer=yes
	SOUNDSYSTEM_DEPS="gstreamer-$GST_MAJORMINOR"
	AC_SUBST(SOUNDSYSTEM_DEPS)

	AC_DEFINE(HAVE_GSTREAMER,1,[Define if you want to use the GStreamer media framework])
	
	RHYTHMBOX_CFLAGS="$RHYTHMBOX_CFLAGS $SOUNDSYSTEM_CFLAGS"
	RHYTHMBOX_LIBS="$RHYTHMBOX_LIBS $SOUNDSYSTEM_LIBS"
	
	dnl Check for gstreamer elements
	AM_GST_ELEMENT_CHECK(gnomevfssrc,,missing_gst_elements="$missing_gst_elements gnomevfssrc")
	AM_GST_ELEMENT_CHECK(spider,,missing_gst_elements="$missing_gst_elements spider")
	AM_GST_ELEMENT_CHECK(volume,,missing_gst_elements="$missing_gst_elements volume")
	AM_GST_ELEMENT_CHECK(osssink,, AM_GST_ELEMENT_CHECK(alsasink,,missing_gst_output_element=yes))

else
	AC_MSG_ERROR([Unknown player $with_player])
fi
fi

AM_CONDITIONAL(USE_XINE, test x"$with_player" = "xxine")

dnl Tagging
AC_ARG_WITH(metadata,
              AC_HELP_STRING([--with-metadata=gstreamer|xine|auto],
			     [Select the metadata reader to use]),,
	      with_metadata=auto)
if test x"$with_player" = xxine && test x"$with_metadata" = xgstreamer; then
  AC_MSG_ERROR([cannot use xine as player with gstreamer for metadata])
fi  
if test x"$with_player" = xgstreamer && test x"$with_metadata" = xxine; then
  AC_MSG_ERROR([cannot use gstreamer as player with xine for metadata])
fi

if test x"$with_metadata" = xauto; then
   if test x"$with_player" = xgstreamer; then
      with_metadata=gstreamer
   else
      with_metadata=xine
   fi
fi

AM_CONDITIONAL(WITH_XINE_METADATA, test x"$with_metadata" = "xxine")
AM_CONDITIONAL(WITH_GST_METADATA, test x"$with_metadata" = "xgstreamer")

dnl AC_CHECK_LIB(lirc_client, lirc_init,
dnl 		[ AC_CHECK_HEADER(lirc/lirc_client.h,
dnl 			[ RHYTHMBOX_LIBS="$RHYTHMBOX_LIBS -llirc_client"
dnl 			AC_DEFINE(HAVE_REMOTE)]
dnl 			,,) ] ,)


dnl Check for Musicbrainz
dnl DISABLED
dnl AC_ARG_ENABLE(musicbrainz, AC_HELP_STRING([--disable-musicbrainz],
dnl				[don't build with MusicBrainz support]))
enable_musicbrainz=no
if test x"$enable_musicbrainz" != "xno"; then
	PKG_CHECK_MODULES(MUSICBRAINZ, libmusicbrainz >= 2.0.0, enable_musicbrainz=yes,
		          enable_musicbrainz=no)
fi
if test x"$enable_musicbrainz" = xyes; then
	RHYTHMBOX_CFLAGS="$RHYTHMBOX_CFLAGS $MUSICBRAINZ_CFLAGS"
	RHYTHMBOX_LIBS="$RHYTHMBOX_LIBS $MUSICBRAINZ_LIBS"
	AC_DEFINE(HAVE_MUSICBRAINZ, 1, [define if you have Musicbrainz])
fi
AM_CONDITIONAL(HAVE_MUSICBRAINZ,test "x$enable_musicbrainz" = "xyes")

AC_PATH_X

CFLAGS="-I$x_includes $CFLAGS"
LIBS="-L$x_libraries $LIBS"

have_xidle=no
AC_COMPILE_IFELSE([
	#include <X11/extensions/xidle.h>
int main(int argc,char **argv) {
  return 0;
}
], have_xidle=yes)
AC_MSG_CHECKING(for XIDLE extension)
AC_MSG_RESULT($have_xidle)
if test x"$have_xidle" = "xyes" ; then
	AC_DEFINE(HAVE_XIDLE_EXTENSION, 1, [defined if you have X11/extensions/xidle.h])
fi

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

dnl CD Audio support		  
dnl AC_ARG_ENABLE(audiocd, AC_HELP_STRING([--enable-audiocd],
dnl				[build with Audio CD support]),enable_audiocd=yes)
dnl For now.
enable_audiocd=no

if test "x$enable_audiocd" = "xyes"; then
  if test x"$enable_musicbrainz" != xyes; then
	AC_MSG_ERROR([MusicBrainz is required for AudioCD support])
  fi
  AC_CHECK_HEADER(linux/cdrom.h,[enable_linux_cdrom=yes],)
  if test "x$enable_linux_cdrom" = "xyes"; then
    AC_MSG_CHECKING([whether linux/cdrom.h actually works])
    AC_COMPILE_IFELSE([
#include <stdlib.h>    
#include <sys/ioctl.h>    
#include <linux/cdrom.h>
int main(int argc,char **argv) {
  ioctl (0, CDROM_GET_CAPABILITY, 0);
  ioctl (0, CDROM_DRIVE_STATUS, CDSL_CURRENT);  
  ioctl (0, CDROMREADTOCHDR, NULL);
  exit(0);
}], [enable_linux_cdrom=yes],[enable_linux_cdrom=no])
    if test "x$enable_linux_cdrom" = "xyes"; then
      AC_MSG_RESULT([yes])
      enable_audiocd=yes
    else
      AC_MSG_FAILURE([linux/cdrom.h is damaged; fix your system or use --disable-audiocd])
    fi
  fi
else
  AC_MSG_NOTICE([CD audio support disabled])
fi
if test "x$enable_audiocd" = "xyes"; then
  if test x"$have_gstreamer" = xyes; then
    AM_GST_ELEMENT_CHECK(cdparanoia,,missing_gst_elements="$missing_gst_elements cdparanoia")
  fi
  AC_DEFINE(HAVE_AUDIOCD,,[Define if we have CD Audio support])
  AC_SUBST(AUDIOCD_INCLUDES, ["#include \"monkey-media-audio-cd.h\""])
else
  :
  AC_SUBST(AUDIOCD_INCLUDES, [])
fi
AM_CONDITIONAL(HAVE_AUDIOCD, test "x$enable_audiocd" = "xyes")
AM_CONDITIONAL(HAVE_LINUX_CDROM, test "x$enable_linux_cdrom" = "xyes")

AC_SUBST(CFLAGS)
AC_SUBST(LDFLAGS)

AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)
AC_PATH_PROG(GCONFTOOL, gconftool-2)

AC_SUBST(RHYTHMBOX_CFLAGS)
AC_SUBST(RHYTHMBOX_LIBS)

ALL_LINGUAS="am ar az be bg ca cs cy da de el en_CA en_GB es et eu fi fr ga he hr hu id is it ja ko lt lv mn ms nl no pa pl pt pt_BR ro ru sk sr sr@Latn sv tr uk vi zh_TW zh_CN"

GETTEXT_PACKAGE=rhythmbox
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [Define to the Gettext package name])

AM_GLIB_GNU_GETTEXT

dnl Workaround for automake 1.8
AC_SUBST(mkdir_p) if test x"$mkdir_p" = "x"; then
  MKINSTALLDIRS="mkinstalldirs"
fi
AC_SUBST(MKINSTALLDIRS)

AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)

AC_ARG_WITH(bonobo,
            AC_HELP_STRING([--with-bonobo],
			   [Enable bonobo support]),,
	      with_bonobo=auto)
if test "x$with_bonobo" != "xno"; then
	PKG_CHECK_MODULES(BONOBO, libbonobo-2.0 >= 2.0.0, enable_bonobo=yes,
		          enable_bonobo=no)
        if test "x$with_bonobo" = xyes && test "x$enable_bonobo" = xno; then
	   AC_MSG_ERROR([Bonobo explicitly requested but no support found])	   
	fi
        if test "x$enable_bonobo" = xyes; then
		AC_DEFINE(WITH_BONOBO, 1, [Define if Bonobo is enabled])	   
		ORBIT_IDL="`$PKG_CONFIG --variable=orbit_idl ORBit-2.0`"
		AC_SUBST(ORBIT_IDL)

		LIBBONOBO_IDL="`$PKG_CONFIG --variable=idldir libbonobo-2.0`"
		AC_SUBST(LIBBONOBO_IDL)

		BONOBO_ACTIVATION_IDL="`$PKG_CONFIG --variable=idldir bonobo-activation-2.0`"
		AC_SUBST(BONOBO_ACTIVATION_IDL)
	fi
fi			  
AM_CONDITIONAL(WITH_BONOBO, test "x$enable_bonobo" = "xyes")
	      
AC_ARG_WITH(dbus,
            AC_HELP_STRING([--with-dbus],
			   [Enable D-BUS support]),,
	      with_dbus=auto)
if test "x$with_dbus" != "xno"; then
	PKG_CHECK_MODULES(DBUS, dbus-glib-1, enable_dbus=yes, enable_dbus=no)
        if test "x$with_dbus" = xyes && test "x$enable_dbus" = xno; then
	   AC_MSG_ERROR([D-BUS explicitly requested but no support found])	   
	fi
        if test "x$enable_dbus" = xyes; then
		AC_DEFINE(HAVE_DBUS, 1, [Define if D-BUS is enabled])	   
		DBUS_CFLAGS="-DDBUS_API_SUBJECT_TO_CHANGE"
		AC_SUBST(DBUS_CFLAGS)
	fi
fi			  
AM_CONDITIONAL(WITH_DBUS, test "x$enable_dbus" = "xyes")

AM_GCONF_SOURCE_2

AC_ARG_ENABLE(uninstalled-build,
              AC_HELP_STRING([--enable-uninstalled-build],
			     [Search for files in build directory as well]),
	      enable_uninstalled=yes,)
if test x"$enable_uninstalled" = xyes; then
	AC_DEFINE_UNQUOTED(SHARE_UNINSTALLED_DIR,"`pwd`/data",[path to source data dir])
fi

dnl warnings bits, copied from gnome-keyring configure.in
dnl Turn on the additional warnings last, so -Werror doesn't affect other tests.

AC_ARG_ENABLE(more-warnings,
[  --enable-more-warnings  Maximum compiler warnings],
set_more_warnings="$enableval",[
if test -d "$srcdir/{arch}" || test -d "$srcdir/CVS"; then
	set_more_warnings=yes
else
	set_more_warnings=no
fi
])
AC_MSG_CHECKING(for more warnings, including -Werror)
if test "$GCC" = "yes" -a "$set_more_warnings" != "no"; then
	AC_MSG_RESULT(yes)
	CFLAGS="\
	-Wall \
	-Wchar-subscripts -Wmissing-declarations -Wmissing-prototypes \
	-Wnested-externs -Wpointer-arith \
	-Wcast-align -Wsign-compare \
	-Werror -std=c89 \
	$CFLAGS"

	for option in -Wno-strict-aliasing -Wno-sign-compare -Wdeclaration-after-statement; do
		SAVE_CFLAGS="$CFLAGS"
		CFLAGS="$CFLAGS $option"
		AC_MSG_CHECKING([whether gcc understands $option])
		AC_TRY_COMPILE([], [],
			has_option=yes,
			has_option=no,)
		if test $has_option = no; then
			CFLAGS="$SAVE_CFLAGS"
		fi
		AC_MSG_RESULT($has_option)
		unset has_option
		unset SAVE_CFLAGS
	done
	unset option
else
	AC_MSG_RESULT(no)
fi

AC_OUTPUT([ 
Makefile
lib/Makefile
metadata/Makefile
player/Makefile
rhythmdb/Makefile
widgets/Makefile
iradio/Makefile
remote/Makefile
remote/bonobo/Makefile
shell/Makefile
data/Makefile
data/ui/Makefile
data/art/Makefile
data/glade/Makefile
data/rhythmbox.pc
sources/Makefile
help/Makefile
help/C/Makefile
po/Makefile.in
tests/Makefile
rhythmbox.spec
])

AC_MSG_NOTICE([Rhythmbox was configured with the following options:])
if test x"$with_database" = xtree; then
	AC_MSG_NOTICE([** Tree database is enabled])
elif test x"$with_database" = xlibgda; then
	AC_MSG_NOTICE([** libgda/sqlite database is enabled])
else
	AC_MSG_ERROR([Unknown database!])
fi
if test x"${with_rhythmdb_debug}" != x0; then
	AC_MSG_NOTICE([** RhythmDB sanity checking enabled (may be slow!)])
fi
if test x"$with_metadata" = xxine; then
	AC_MSG_NOTICE([** Xine metadata is enabled])
else
	AC_MSG_NOTICE([** GStreamer metadata is enabled])
fi
if test x"$enable_mmkeys" != "xyes"; then
	AC_MSG_NOTICE([   Multimedia keys support is disabled])
else
	AC_MSG_NOTICE([** Multimedia keys support is enabled])
fi
if test x"$enable_dashboard" != "xyes"; then
	AC_MSG_NOTICE([   Dashboard support is disabled])
else
	AC_MSG_NOTICE([** Dashboard support is enabled])
fi
dnl if test x"$enable_musicbrainz" != "xyes"; then
dnl 	AC_MSG_NOTICE([   MusicBrainz support is disabled])
dnl else
dnl 	AC_MSG_NOTICE([** MusicBrainz support is enabled])
dnl fi
if test x"$with_player" = xgstreamer; then
	AC_MSG_NOTICE([** GStreamer $GST_MAJORMINOR player is enabled])
else
	AC_MSG_NOTICE([** xine player is enabled])
fi
if test x"$enable_ipod" = xyes; then
	AC_MSG_NOTICE([** iPod integration enabled])
else
	AC_MSG_NOTICE([   iPod integration disabled])
fi
if test x"$enable_bonobo" = xyes; then
	AC_MSG_NOTICE([** Bonobo control/activation enabled])
else
	AC_MSG_NOTICE([   Bonobo control/activation disabled])
fi
if test x"$enable_dbus" = xyes; then
	AC_MSG_NOTICE([** D-BUS control/activation enabled])
else
	AC_MSG_NOTICE([   D-BUS control/activation disabled])
fi

dnl if test "x$enable_audiocd" != "xyes"; then
dnl 	AC_MSG_NOTICE([   Audio CD support is disabled])
dnl else
dnl 	AC_MSG_NOTICE([** Audio CD support is enabled])
dnl 	AC_MSG_WARN([Audio CD support is experimental!])
dnl fi
if test x"$missing_gst_output_element" != x; then
	AC_MSG_WARN([Neither osssink nor alsasink could be found on your system])
fi	
if test x"$missing_gst_elements" != x; then
	AC_MSG_WARN([The following GStreamer elements could not be found at compile time: $missing_gst_elements])
	AC_MSG_WARN([Rhythmbox will fail at runtime if these elements cannot be found])
fi
AC_MSG_NOTICE([End options])
