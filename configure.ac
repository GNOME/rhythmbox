define(arch-tag)
# arch-tag: Toplevel Autoconf configuration script
AC_PREREQ(2.53)
AC_INIT(shell)
AM_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE(rhythmbox, 0.6.5)

AM_MAINTAINER_MODE

AC_PROG_INTLTOOL

AC_ISC_POSIX
AC_PROG_CC
AC_STDC_HEADERS
AM_PROG_LIBTOOL
AC_C_BIGENDIAN
AC_CHECK_SIZEOF(long)

ACLOCAL="$ACLOCAL -I macros"

if test x"$GCC" = xyes; then
  AC_MSG_NOTICE([disabling compiler optimiztation by default])
  CFLAGS=`echo $CFLAGS | sed -e 's/-O2//'`
fi

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

PKG_CHECK_MODULES(RHYTHMBOX,                    \
                  gtk+-2.0 >= 2.2.2             \
                  libgnomeui-2.0                \
		  libglade-2.0                  \
		  gnome-vfs-2.0                 \
		  gnome-vfs-module-2.0		\
		  libbonobo-2.0			\
		  bonobo-activation-2.0)

RHYTHMBOX_CFLAGS="$CFLAGS $RHYTHMBOX_CFLAGS"

dnl Database

AC_ARG_WITH(database,
              AC_HELP_STRING([--database],
			     [Select the database to use (default: tree)]),,
	      with_database=tree)
AM_CONDITIONAL(USE_TREEDB, test x"$with_database" = xtree)
if test x"$with_database" != xtree; then
	AC_MSG_ERROR([Unknown database selected])
else
	AC_DEFINE(WITH_RHYTHMDB_TREE, 1, [Define if you are using the RhythmDB tree database])
fi	

dnl dashboard support
AC_ARG_ENABLE(dashboard,
	      [  --enable-dashboard	  Enable dashboard support (http://www.nat.org/dashboard) (default disabled)],enable_dashboard=yes,enable_dashboard=no)
if test "x$enable_dashboard" == "xyes" ; then
   AC_DEFINE([WITH_DASHBOARD], 1, [Define to enable support for dashboard (http://www.nat.org/dashboard)])
fi
AM_CONDITIONAL(WITH_DASHBOARD, test x"$enable_dashboard" = xyes)

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
			gstreamer-$GST_MAJORMINOR >= 0.6.2 gstreamer-libs-$GST_MAJORMINOR
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
	AM_GST_ELEMENT_CHECK(gnomevfssrc,,missing_gst_elements="$missing_gst_elements gnomevfssrc")
	AM_GST_ELEMENT_CHECK(spider,,missing_gst_elements="$missing_gst_elements spider")
	AM_GST_ELEMENT_CHECK(volume,,missing_gst_elements="$missing_gst_elements volume")
	AM_GST_ELEMENT_CHECK(osssink,, AM_GST_ELEMENT_CHECK(alsasink,,missing_gst_output_element=yes))

fi

AC_ARG_ENABLE(gstreamer_workarounds,
              AC_HELP_STRING([--disable-gstreamer-workarounds],
			     [Enable use of the GStreamer backend without workarounds]),,
	      disable_gstreamer_workarounds=no)

AM_CONDITIONAL(USE_XINE, test x"$WANT_XINE" = "x1")
AM_CONDITIONAL(USE_GSTREAMER_WORKAROUNDS, test x"$disable_gstreamer_workarounds" != xyes)

dnl Workaround for the gtktreeview bug
disable_gtktreeview_workaround=yes
AC_ARG_ENABLE(gtktreeview_workaround,
              AC_HELP_STRING([--disable-gtktreeview-workaround],
	                     [Disable use of a workaround for the GtkTreeView bug which causes temporary display corruption]),,
	      disable_gtktreeview_workaround=no)

if test "x$disable_gtktreeview_workaround" != "xyes"; then
  AC_DEFINE(USE_GTK_TREE_VIEW_WORKAROUND,,[Define to enable the GtkTreeView workaround])
fi

dnl Stolen from D-BUS configury
dnl Atomic integers (checks by Sebastian Wilhelmi for GLib)
AC_MSG_CHECKING([whether to use inline assembler routines for atomic integers])
have_atomic_inc=no
if test x"$GCC" = xyes; then
  if test "x$enable_ansi" = "xyes"; then
    AC_MSG_RESULT([no])
  else
    case $host_cpu in
      i386)
        AC_MSG_RESULT([no])
        ;;
      i?86)
        AC_MSG_RESULT([i486])
        AC_DEFINE_UNQUOTED(RB_USE_ATOMIC_INT_486, 1, [Use atomic integer implementation for 486])
        have_atomic_inc=yes
        ;;
      *)
        AC_MSG_RESULT([no])
        ;;
    esac
  fi
fi
if test x$have_atomic_inc = xyes ; then
  AC_DEFINE_UNQUOTED(RB_HAVE_ATOMIC_INT, 1, [Some atomic integer implementation present])
fi

dnl AC_CHECK_LIB(lirc_client, lirc_init,
dnl 		[ AC_CHECK_HEADER(lirc/lirc_client.h,
dnl 			[ RHYTHMBOX_LIBS="$RHYTHMBOX_LIBS -llirc_client"
dnl 			AC_DEFINE(HAVE_REMOTE)]
dnl 			,,) ] ,)

dnl Check for libid3tag

AC_ARG_ENABLE(mp3, AC_HELP_STRING([--disable-mp3],
				[don't build with MP3 support]))
if test "x$enable_mp3" != "xno"; then
  PKG_CHECK_MODULES(ID3TAG, id3tag >= 0.15.0b, enable_mp3=yes, enable_mp3=no)
  if test x"$enable_mp3" = xno; then
    AC_CHECK_HEADER(id3tag.h,[enable_mp3=yes],)
    if test x"$enable_mp3" = xyes; then
	AC_CHECK_LIB(id3tag,id3_frame_field,[enable_mp3=yes],[enable_mp3=no])
        if test x"$enable_mp3" = xno; then
          AC_MSG_ERROR([For MP3 support, you need libid3tag 15.0b or later (or --disable-mp3)])
	fi
        ID3TAG_LIBS="-lid3tag"
    fi
  fi
fi
if test "x$enable_mp3" = "xyes"; then
  if test x"$have_gstreamer" = xyes; then
    AM_GST_ELEMENT_CHECK(mad,,missing_gst_elements="$missing_gst_elements mad")
  fi
  AC_SUBST(ID3TAG_LIBS)
fi
if test "x$enable_mp3" = "xyes"; then
   AC_DEFINE(HAVE_MP3,,[Define if we have MP3 support])
fi
AM_CONDITIONAL(HAVE_MP3,test "x$enable_mp3" = "xyes")

dnl Check for libmp4v2

AC_ARG_ENABLE(mp4, AC_HELP_STRING([--disable-mp4],
			[don't build with MPEG-4 support]))
if test "x$enable_mp4" != "xno"; then
  AC_CHECK_HEADER(mp4.h, [AC_CHECK_HEADER(mpeg4ip.h, [enable_mp4=yes],)],)
fi
if test "x$enable_mp4" = "xyes"; then
  LIBS="$LIBS -lmp4v2 -lstdc++"
  AC_DEFINE(HAVE_MP4,,[Define if we have MPEG-4 support])
fi

AM_CONDITIONAL(HAVE_MP4, test "x$enable_mp4" = "xyes")

dnl Check for Ogg Vorbis

AC_ARG_ENABLE(vorbis, AC_HELP_STRING([--disable-vorbis],
				[don't build with Ogg Vorbis support]))
if test x"$enable_vorbis" != xno; then
  MM_PATH_OGG(enable_vorbis=yes,enable_vorbis=no)
  if test x"$enable_vorbis" = xyes; then
    MM_PATH_VORBIS(,enable_vorbis=no)
  fi
  if test x"$enable_vorbis" = xyes; then
    if test x"$have_gstreamer" = xyes; then
      AM_GST_ELEMENT_CHECK(vorbisfile,,missing_gst_elements="$missing_gst_elements vorbis")
    fi
    CFLAGS="$CFLAGS $OGG_CFLAGS $VORBIS_CFLAGS"
    LIBS="$LIBS $OGG_LIBS $VORBIS_LIBS $VORBISFILE_LIBS"
  fi
fi  
if test "x$enable_vorbis" = "xyes"; then
   AC_DEFINE(HAVE_VORBIS,,[Define if we have Ogg Vorbis support])
fi
AM_CONDITIONAL(HAVE_VORBIS,test "x$enable_vorbis" = "xyes")

dnl Check for FLAC

AC_ARG_ENABLE(flac, AC_HELP_STRING([--disable-flac],
				[don't build with flac support]))
if test x"$enable_flac" = xyes && test x"$enable_mp3" != xyes; then
  AC_MSG_ERROR([libid3tag is required for FLAC support])  
fi
if test "$xenable_flac" != "xno" && test x"$enable_mp3" = xyes; then
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

AM_CONDITIONAL(USE_ID3VFS, test x"$enable_mp3" = "xyes" || test x"$enable_flac" = "xyes")

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

ALL_LINGUAS="am az be ca cs cy da de el es fi fr ga id it ja ko lv ms nl no pl pt pt_BR ro ru sr sr@Latn sv tr vi zh_TW"

GETTEXT_PACKAGE=rhythmbox
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [Define to the Gettext package name])

AM_GLIB_GNU_GETTEXT

AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)

AM_GCONF_SOURCE_2

ORBIT_IDL="`$PKG_CONFIG --variable=orbit_idl ORBit-2.0`"
AC_SUBST(ORBIT_IDL)

LIBBONOBO_IDL="`$PKG_CONFIG --variable=idldir libbonobo-2.0`"
AC_SUBST(LIBBONOBO_IDL)

BONOBO_ACTIVATION_IDL="`$PKG_CONFIG --variable=idldir bonobo-activation-2.0`"
AC_SUBST(BONOBO_ACTIVATION_IDL)

dnl uninstalled share dir
AC_DEFINE_UNQUOTED(SHARE_UNINSTALLED_DIR,"`pwd`/data",[path to source data dir])
AC_SUBST(DATADIR_UNINST)

AC_OUTPUT([ 
Makefile
lib/Makefile
rhythmdb/Makefile
widgets/Makefile
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
sources/Makefile
help/Makefile
help/C/Makefile
po/Makefile.in
component/Makefile
tests/Makefile
rhythmbox.spec
])

AC_MSG_NOTICE([Rhythmbox was configured with the following options:])
if test x"$with_database" = xtree; then
	AC_MSG_NOTICE([** Tree database is enabled])
fi
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
if test x"$enable_mp4" != "xyes"; then
	AC_MSG_NOTICE([   MPEG-4 support is disabled])
else
	AC_MSG_NOTICE([** MPEG-4 support is enabled])
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
if test x"$WANT_XINE" != "x1"; then
	if test x"$disable_gstreamer_workarounds" != xyes; then	
		AC_MSG_NOTICE([** gstreamer $GST_MAJORMINOR backend is enabled (with workarounds)])
	else
		AC_MSG_NOTICE([** gstreamer $GST_MAJORMINOR backend is enabled])
	fi
else
	AC_MSG_NOTICE([** xine backend is enabled])
fi
dnl if test "x$enable_audiocd" != "xyes"; then
dnl 	AC_MSG_NOTICE([   Audio CD support is disabled])
dnl else
dnl 	AC_MSG_NOTICE([** Audio CD support is enabled])
dnl 	AC_MSG_WARN([Audio CD support is experimental!])
dnl fi
if test x"$disable_gtktreeview_workaround" = xyes; then
	AC_MSG_NOTICE([   GtkTreeView workaround disabled])
else
	AC_MSG_NOTICE([** GtkTreeView workaround enabled])
fi
if test x"$missing_gst_output_element" != x; then
	AC_MSG_WARN([Neither osssink nor alsasink could be found on your system])
fi	
if test x"$missing_gst_elements" != x; then
	AC_MSG_WARN([The following GStreamer elements could not be found at compile time: $missing_gst_elements])
	AC_MSG_WARN([Rhythmbox will fail at runtime if these elements cannot be found])
fi
AC_MSG_NOTICE([End options])
