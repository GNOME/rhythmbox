AC_PREREQ(2.53)
AC_INIT(shell)
AM_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE(rhythmbox, 0.4.99)

AM_MAINTAINER_MODE

AC_PROG_INTLTOOL

AC_ISC_POSIX
AC_PROG_CC
AC_STDC_HEADERS
AM_PROG_LIBTOOL
AC_C_BIGENDIAN
AC_CHECK_SIZEOF(long)

if test x"$GCC" = "xyes"; then
AC_MSG_CHECKING([gcc argument to enable C99])
saved_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -std=c99"
AC_COMPILE_IFELSE([
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) { printf("%s\n", __func__); exit(0); }
],[AC_MSG_RESULT([-std=c99])],[CFLAGS="$saved_CFLAGS"; AC_MSG_RESULT([C99 not available])])
fi

PKG_CHECK_MODULES(RHYTHMBOX,                    \
                  gtk+-2.0 >= 2.2.0             \
                  libgnomeui-2.0                \
		  libglade-2.0                  \
		  gnome-vfs-2.0                 \
		  gnome-vfs-module-2.0		\
		  libbonobo-2.0			\
		  bonobo-activation-2.0)

RHYTHMBOX_CFLAGS="$CFLAGS $RHYTHMBOX_CFLAGS"

dnl Sound system
WANT_XINE=
AC_ARG_ENABLE(xine,
              [  --enable-xine		Enable use of xine instead of gstreamer],
	      WANT_XINE=1)
if test "x$WANT_XINE" = "x1"; then
	SOUNDSYSTEM_DEPS="libxine >= 1.0.0"
	AC_SUBST(SOUNDSYSTEM_DEPS)

	PKG_CHECK_MODULES(SOUNDSYSTEM, $SOUNDSYSTEM_DEPS)

	AC_DEFINE(HAVE_XINE,1,[define if you want to use the Xine libraries])

	RHYTHMBOX_CFLAGS="$RHYTHMBOX_CFLAGS $SOUNDSYSTEM_CFLAGS"
	RHYTHMBOX_LIBS="$RHYTHMBOX_LIBS $SOUNDSYSTEM_LIBS"
else
	dnl Now we're ready to ask for gstreamer libs and cflags
	dnl And we can also ask for the right version of gstreamer
	HAVE_SOUNDSYSTEM=no

	dnl start with 0.7
	GST_MAJORMINOR=0.7
	PKG_CHECK_MODULES(SOUNDSYSTEM, \
		gstreamer-$GST_MAJORMINOR gstreamer-libs-$GST_MAJORMINOR
		gstreamer-control-$GST_MAJORMINOR gstreamer-gconf-$GST_MAJORMINOR,
		HAVE_SOUNDSYSTEM=yes,HAVE_SOUNDSYSTEM=no)

	dnl try 0.6
	if test "x$HAVE_SOUNDSYSTEM" = "xno"; then
		GST_MAJORMINOR=0.6
		PKG_CHECK_MODULES(SOUNDSYSTEM, \
			gstreamer-$GST_MAJORMINOR gstreamer-libs-$GST_MAJORMINOR
			gstreamer-control-$GST_MAJORMINOR gstreamer-gconf-$GST_MAJORMINOR,
			HAVE_SOUNDSYSTEM=yes,HAVE_SOUNDSYSTEM=no)
	fi

        dnl fail back to 0.5
	if test "x$HAVE_SOUNDSYSTEM" = "xno"; then
		GST_MAJORMINOR=0.5
		PKG_CHECK_MODULES(SOUNDSYSTEM, \
			gstreamer-$GST_MAJORMINOR >= 0.5.2 gstreamer-libs-$GST_MAJORMINOR >= 0.5.2
			gstreamer-control-$GST_MAJORMINOR gstreamer-gconf-$GST_MAJORMINOR,
			HAVE_SOUNDSYSTEM=yes,HAVE_SOUNDSYSTEM=no)
	fi

	dnl Give error and exit if we don't have gstreamer
	if test "x$HAVE_SOUNDSYSTEM" = "xno"; then
		AC_MSG_ERROR([GStreamer not found, or too old version found])
	fi
	have_gstreamer=yes
	SOUNDSYSTEM_DEPS="gstreamer-$GST_MAJORMINOR"
	AC_SUBST(SOUNDSYSTEM_DEPS)

	AC_DEFINE(HAVE_GSTREAMER,1,[Define if you want to use the GStreamer media framework])
	
	RHYTHMBOX_CFLAGS="$RHYTHMBOX_CFLAGS $SOUNDSYSTEM_CFLAGS"
	RHYTHMBOX_LIBS="$RHYTHMBOX_LIBS $SOUNDSYSTEM_LIBS"
	
	dnl Check for gstreamer elements
	AM_GST_ELEMENT_CHECK(gnomevfssrc,,
			     AC_MSG_ERROR(gnomevfssrc element needed!))
	AM_GST_ELEMENT_CHECK(spider,,
			     AC_MSG_ERROR(spider element needed!))
	AM_GST_ELEMENT_CHECK(volume,,
			     AC_MSG_ERROR(volume element needed!))
	AM_GST_ELEMENT_CHECK(osssink,,
			     AM_GST_ELEMENT_CHECK(alsasink,,
		                          AC_MSG_RESULT(neither osssink or alsasink seems to be installed, audio output may not work)))

fi

USE_BROKEN_GSTREAMER=1
AC_ARG_ENABLE(broken_gstreamer,
              [  --disable-broken-gstreamer		Enable use of the GStreamer backend without workarounds],
	      USE_BROKEN_GSTREAMER=0)
if test x$enable_broken_gstreamer != xno; then
	AC_DEFINE(USE_BROKEN_GSTREAMER,1,[Define if you don't want to use GStreamer bug workarounds])
fi

dnl Check for zlib

AC_CHECK_HEADER(zlib.h,,AC_MSG_ERROR(zlib needed!))
LIBS="$LIBS -lz"

dnl AC_CHECK_LIB(lirc_client, lirc_init,
dnl 		[ AC_CHECK_HEADER(lirc/lirc_client.h,
dnl 			[ RHYTHMBOX_LIBS="$RHYTHMBOX_LIBS -llirc_client"
dnl 			AC_DEFINE(HAVE_REMOTE)]
dnl 			,,) ] ,)

dnl Check for libid3tag

AC_ARG_ENABLE(mp3, AC_HELP_STRING([--disable-mp3],
				[don't build with MP3 support]))
if test "x$enable_mp3" != "xno"; then
  AC_CHECK_HEADER(id3tag.h,[enable_mp3=yes],)
fi
if test "x$enable_mp3" = "xyes"; then
  if test x"$have_gstreamer" = xyes; then
    AM_GST_ELEMENT_CHECK(mad,,
		         AC_MSG_RESULT(mad element missing, MP3 playback will not work!))
  fi
  ID3TAG_LIBS="-lid3tag"
  AC_SUBST(ID3TAG_LIBS)
fi
if test "x$enable_mp3" = "xyes"; then
   AC_DEFINE(HAVE_MP3,,[Define if we have MP3 support])
fi
AM_CONDITIONAL(HAVE_MP3,test "x$enable_mp3" = "xyes")

dnl Check for Ogg Vorbis

MM_PATH_OGG(enable_vorbis=yes,enable_vorbis=no)
if test x"$enable_vorbis" = xyes; then
  MM_PATH_VORBIS(,enable_vorbis=no)
fi
if test x"$enable_vorbis" = xyes; then
  if test x"$have_gstreamer" = xyes; then
    AM_GST_ELEMENT_CHECK(vorbisfile,,
			 AC_MSG_WARN(vorbisfile element missing, Ogg Vorbis playback will not work!))
  fi
  CFLAGS="$CFLAGS $OGG_CFLAGS $VORBIS_CFLAGS"
  LIBS="$LIBS $OGG_LIBS $VORBIS_LIBS $VORBISFILE_LIBS"
fi
if test "x$enable_vorbis" = "xyes"; then
   AC_DEFINE(HAVE_VORBIS,,[Define if we have Ogg Vorbis support])
fi
AM_CONDITIONAL(HAVE_VORBIS,test "x$enable_vorbis" = "xyes")

dnl Check for FLAC

AC_ARG_ENABLE(flac, AC_HELP_STRING([--disable-flac],
				[don't build with flac support]))
if test "x$enable_flac" != "xno"; then
  AC_CHECK_HEADER(FLAC/all.h,[enable_flac=yes],)
fi
if test "x$enable_flac" = "xyes"; then
  LIBS="$LIBS -lFLAC"
fi  
if test "x$enable_flac" = "xyes"; then
   AC_DEFINE(HAVE_FLAC,,[Define if we have FLAC support])
fi
AM_CONDITIONAL(HAVE_FLAC,test "x$enable_flac" = "xyes")

CFLAGS="$CFLAGS $OGG_CFLAGS $VORBIS_CFLAGS"
LIBS="$LIBS $OGG_LIBS $VORBIS_LIBS $VORBISFILE_LIBS $FLAC_LIBS"

dnl Check for Musicbrainz
AC_ARG_ENABLE(musicbrainz, AC_HELP_STRING([--disable-musicbrainz],
				[don't build with MusicBrainz support]))
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
    AM_GST_ELEMENT_CHECK(cdparanoia,,
			  AC_MSG_WARN(cdparanoia element required))
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

AC_ARG_ENABLE(install_schemas,
	[  --disable-install-schemas        Disable installation of the gconf schemas])
AM_CONDITIONAL(INSTALL_SCHEMAS, test x$enable_install_schemas != xno)

AC_SUBST(RHYTHMBOX_CFLAGS)
AC_SUBST(RHYTHMBOX_LIBS)

ALL_LINGUAS="am be cs da de el es fi fr it ja ko lv ms nl no pl pt pt_BR ru sr sr@Latn sv tr vi zh_TW"

GETTEXT_PACKAGE=rhythmbox
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [Define to the Gettext package name])

AM_GLIB_GNU_GETTEXT

AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)

AC_PATH_PROG(GCONFTOOL, gconftool-2)

ORBIT_IDL="`$PKG_CONFIG --variable=orbit_idl ORBit-2.0`"
AC_SUBST(ORBIT_IDL)

LIBBONOBO_IDL="`$PKG_CONFIG --variable=idldir libbonobo-2.0`"
AC_SUBST(LIBBONOBO_IDL)

BONOBO_ACTIVATION_IDL="`$PKG_CONFIG --variable=idldir bonobo-activation-2.0`"
AC_SUBST(BONOBO_ACTIVATION_IDL)

AC_ARG_ENABLE(install_schemas,
	[  --disable-install-schemas        Disable installation of the gconf schemas])
AM_CONDITIONAL(INSTALL_SCHEMAS, test x$enable_install_schemas != xno)

if test x$enable_install_schemas != xno; then
	AM_GCONF_SOURCE_2
fi

dnl uninstalled share dir
AC_DEFINE_UNQUOTED(SHARE_UNINSTALLED_DIR,"`pwd`/data",[path to source data dir])
AC_SUBST(DATADIR_UNINST)

AC_OUTPUT([ 
Makefile
lib/Makefile
lib/widgets/Makefile
lib/egg/Makefile
library/Makefile
iradio/Makefile
corba/Makefile
shell/Makefile
data/Makefile
data/ui/Makefile
data/art/Makefile
data/glade/Makefile
data/node-views/Makefile
data/rhythmbox.pc
monkey-media/Makefile
monkey-media/monkey-media-includes.h
monkey-media/stream-info-impl/Makefile
monkey-media/stream-info-impl/id3-vfs/Makefile
tests/Makefile
sources/Makefile
help/Makefile
help/C/Makefile
po/Makefile.in
rhythmbox.spec
])

AC_MSG_NOTICE([Rhythmbox was configured with the following options:])
if test x"$enable_mp3" != "xyes"; then
	AC_MSG_NOTICE([   MP3 support is disabled])
else
	AC_MSG_NOTICE([** MP3 support is enabled])
fi
if test x"$enable_vorbis" != "xyes"; then
	AC_MSG_NOTICE([   Ogg Vorbis support is disabled])
else
	AC_MSG_NOTICE([** Ogg Vorbis support is enabled])
fi
if test x"$enable_flac" != "xyes"; then
	AC_MSG_NOTICE([   FLAC support is disabled])
else
	AC_MSG_NOTICE([** FLAC support is enabled])
fi
if test x"$enable_musicbrainz" != "xyes"; then
	AC_MSG_NOTICE([   MusicBrainz support is disabled])
else
	AC_MSG_NOTICE([** MusicBrainz support is enabled])
fi
dnl if test "x$enable_audiocd" != "xyes"; then
dnl 	AC_MSG_NOTICE([   Audio CD support is disabled])
dnl else
dnl 	AC_MSG_NOTICE([** Audio CD support is enabled])
dnl 	AC_MSG_WARN([Audio CD support is experimental!])
dnl fi
AC_MSG_NOTICE([End options])
