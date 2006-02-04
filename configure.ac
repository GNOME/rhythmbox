define(arch-tag)
# arch-tag: Toplevel Autoconf configuration script
AC_PREREQ(2.53)
AC_INIT(shell)
AC_CONFIG_MACRO_DIR(macros)
AM_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE(rhythmbox, 0.9.3)

AM_MAINTAINER_MODE

AC_PROG_INTLTOOL

AC_ISC_POSIX
AC_PROG_CC
AC_STDC_HEADERS
AM_PROG_LIBTOOL
AC_C_BIGENDIAN
AC_CHECK_SIZEOF(long)
GTK_REQS=2.5.4
GNOME_VFS_REQS=2.7.4
NCB_MIN_REQS=2.9.0
DBUS_MIN_REQS=0.35
TOTEM_PLPARSER_REQS=1.1.5

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

AC_CHECK_LIB(glib-2.0, g_utf8_collate_key_for_filename,
    [AC_DEFINE([HAVE_COLLATE_KEY_FILENAME], 1, [Have glib function to collate filename sort keys])])

PKG_PROG_PKG_CONFIG

PKG_CHECK_MODULES(RHYTHMBOX,				\
		  gtk+-2.0 >= $GTK_REQS			\
		  libgnomeui-2.0			\
		  libglade-2.0				\
		  gnome-vfs-2.0 >= $GNOME_VFS_REQS	\
		  gnome-vfs-module-2.0)

PKG_CHECK_MODULES(TOTEM_PLPARSER, totem-plparser >= $TOTEM_PLPARSER_REQS, have_totem_plparser=yes, have_totem_plparser=no)
if test x$have_totem_plparser != xyes; then
   AC_MSG_ERROR([totem playlist parsing library not found or too old])
fi   

AC_ARG_WITH(hal,
	      AC_HELP_STRING([--without-hal],
			     [Disable HAL support]))
if test "x$with_hal" != "xno"; then
  PKG_CHECK_MODULES(HAL, hal >= 0.5 hal < 0.6, enable_hal05=yes, enable_hal05=no)
  if test x$enable_hal05 != xyes; then
     PKG_CHECK_MODULES(HAL, hal >= 0.2.92 hal < 0.5, enable_hal02=yes, enable_hal02=no)
  fi
  if test x$enable_hal05 == xyes || test x$enable_hal02 == xyes; then
      enable_hal=yes
      if test x$enable_hal05 == xyes; then
         AC_DEFINE(HAVE_HAL_0_5, 1, [Define if you have HAL 0.5 or later])
      else
         AC_DEFINE(HAVE_HAL_0_2, 1, [Define if you have HAL 0.2 or later])
      fi
  	  AC_SUBST(HAL_CFLAGS)
  	  AC_SUBST(HAL_LIBS)
  else
    if test "x$with_hal" = "xyes"; then
	  AC_MSG_ERROR([HAL support explicitly requested but HAL couldn't be found])
    fi
  fi
fi



dnl iPod support

AC_ARG_WITH(ipod,
            AC_HELP_STRING([--with-ipod],
			   [Enable iPod support]),,
	      with_ipod=auto)
if test "x$with_ipod" != "xno"; then

	PKG_CHECK_MODULES(IPOD, libgpod-1.0, have_libgpod=yes, have_libgpod=no)
	if test "x$have_libgpod" = "xno" -a "x$with_ipod" = "xyes"; then
	  AC_MSG_ERROR([iPod explicitly requested but libgpod couldn't be found])
	fi
	if test "x$have_libgpod" = "xyes"; then
          if test "x$with_hal" = xyes && test "x$enable_hal" = xno; then
	     AC_MSG_ERROR([iPod explicitly requested but HAL not found or too old])	   
	  fi
          if test "x$enable_hal" = xyes; then
	     AC_DEFINE(WITH_IPOD_SUPPORT, 1, [Define if iPod support is enabled])
	     use_ipod=yes
	  fi
	  AC_SUBST(IPOD_CFLAGS)
	  AC_SUBST(IPOD_LIBS)
	fi
fi			  
AM_CONDITIONAL(USE_IPOD, test x"$use_ipod" = xyes)

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

dnl Sound system
dnl Now we're ready to ask for gstreamer libs and cflags
dnl And we can also ask for the right version of gstreamer
HAVE_SOUNDSYSTEM=no

GST_0_8_MAJORMINOR=0.8
GST_0_8_REQUIRED_VERSION=0.8.2
PKG_CHECK_MODULES(GSTREAMER_0_8, \
	gstreamer-$GST_0_8_MAJORMINOR >= $GST_0_8_REQUIRED_VERSION gstreamer-libs-$GST_0_8_MAJORMINOR >= $GST_0_8_REQUIRED_VERSION
	gstreamer-control-$GST_0_8_MAJORMINOR >= $GST_0_8_REQUIRED_VERSION
	gstreamer-gconf-$GST_0_8_MAJORMINOR >= $GST_0_8_REQUIRED_VERSION,
	have_gstreamer_0_8=yes,have_gstreamer_0_8=no)
	
GST_0_10_MAJORMINOR=0.10
GST_0_10_REQUIRED_VERSION=0.9.7
PKG_CHECK_MODULES(GSTREAMER_0_10, \
	gstreamer-$GST_0_10_MAJORMINOR >= $GST_0_10_REQUIRED_VERSION
	gstreamer-base-$GST_0_10_MAJORMINOR >= $GST_0_10_REQUIRED_VERSION
	gstreamer-plugins-base-$GST_0_10_MAJORMINOR >= $GST_0_10_REQUIRED_VERSION,
	have_gstreamer_0_10=yes,have_gstreamer_0_10=no)

AC_ARG_WITH(playback,
   AC_HELP_STRING([--with-playback=auto|gstreamer-0-8|gstreamer-0-10],
   [Select the playback backend to use (default auto)]),,
   with_playback=auto)

if test x$with_playback = xauto -o x$with_playback = xgstreamer-0-10; then
	if test x"$have_gstreamer_0_10" = xyes; then
		with_playback=gstreamer_0_10
		SOUNDSYSTEM_DEPS="gstreamer-$GST_0_10_MAJORMINOR"
		AC_DEFINE(HAVE_GSTREAMER,1,[Define if you want to use the GStreamer media framework])
		AC_DEFINE(HAVE_GSTREAMER_0_10,1,[Define if you want to use the GStreamer 0.10])
		SOUNDSYSTEM_CFLAGS="$GSTREAMER_0_10_CFLAGS"
		SOUNDSYSTEM_LIBS="$GSTREAMER_0_10_LIBS"
	elif test x$with_playback = xgstreamer-0-10; then
		AC_MSG_ERROR([GStreamer 0.10 explicity requested, and it was not found or older than version $GST_REQUIRED_VERSION, or gst-plugins-base was missing])
	fi
fi
if test x$with_playback = xauto -o x$with_playback = xgstreamer-0-8; then
	if test x"$have_gstreamer_0_8" = xyes; then
		with_playback=gstreamer_0_8
		SOUNDSYSTEM_DEPS="gstreamer-$GST_0_8_MAJORMINOR"
		AC_DEFINE(HAVE_GSTREAMER,1,[Define if you want to use the GStreamer media framework])
		AC_DEFINE(HAVE_GSTREAMER_0_8,1,[Define if you want to use the GStreamer 0.8])
		SOUNDSYSTEM_CFLAGS="$GSTREAMER_0_8_CFLAGS"
		SOUNDSYSTEM_LIBS="$GSTREAMER_0_8_LIBS"
	elif test x$with_playback = xgstreamer-0-8; then
		AC_MSG_ERROR([GStreamer 0.8 explicity requested, and it was not found or older than version $GST_REQUIRED_VERSION])
	fi
fi
AM_CONDITIONAL(USE_GSTREAMER_0_10, test x"$with_playback" = xgstreamer_0_10)
AM_CONDITIONAL(USE_GSTREAMER_0_8, test x"$with_playback" = xgstreamer_0_8)

dnl Give error and exit if we don't have gstreamer
if test x$with_playback = xauto; then
		AC_MSG_ERROR([GStreamer not found, or older than version $GST_0_8_REQUIRED_VERSION/$GST_0_10_REQUIRED_VERSION])
fi

AC_SUBST(SOUNDSYSTEM_DEPS)
RHYTHMBOX_CFLAGS="$RHYTHMBOX_CFLAGS $SOUNDSYSTEM_CFLAGS"
RHYTHMBOX_LIBS="$RHYTHMBOX_LIBS $SOUNDSYSTEM_LIBS"


dnl Tag writing
AC_ARG_ENABLE(tag-writing,
	      AC_HELP_STRING([--enable-tag-writing],
			     [Enable tag writing support in rhythmbox **EXPERIMENTAL**]))
if test x"$enable_tag_writing" = xyes; then
   AC_MSG_WARN([Tag writing support is experimental, and may corrupt files, use at your own risk])
   AC_DEFINE(ENABLE_TAG_WRITING, 1, [Define if tag writing should be enabled])
fi


dnl Audioscrobbler
AC_ARG_ENABLE(audioscrobbler,
	      AC_HELP_STRING([--disable-audioscrobbler],
			     [Disable Audioscrobbler support in Rhythmbox]))

dnl DAAP (iTunes Music Shares)
AC_ARG_ENABLE(daap,
	      AC_HELP_STRING([--enable-daap],
			     [Enable Digital Audio Access Protocol (music sharing) in rhythmbox **EXPERIMENTAL**]))

AC_ARG_WITH(mdns,
   AC_HELP_STRING([--with-mdns=auto|howl|avahi],
   [Select the mDNS/DNS-SD implementation to use (default auto)]),,
   with_mdns=auto)

have_howl=no
have_avahi=no
have_mdns=no

PKG_CHECK_MODULES(AVAHI,
   avahi-client >= 0.6
   avahi-glib >= 0.6,
   have_avahi_06=yes,
   have_avahi_06=no)
if test x$have_avahi_06 != xyes; then
   PKG_CHECK_MODULES(AVAHI,
      avahi-client >= 0.5
      avahi-glib >= 0.5,
      have_avahi_05=yes,
      have_avahi_05=no)
fi
if test x$have_avahi_06 == xyes || test x$have_avahi_05 == xyes; then
   have_avahi=yes
else
   have_avahi=no
fi

PKG_CHECK_MODULES(HOWL,
   howl,
   have_howl=yes,
   have_howl=no)

if test x"$with_mdns" = xauto; then
   if test x"$have_avahi" = xyes; then
      MDNS_CFLAGS=$AVAHI_CFLAGS
      MDNS_LIBS=$AVAHI_LIBS
      AC_DEFINE(WITH_AVAHI, 1, [Define if mDNS/DNS-SD implementation uses Avahi])
      AC_MSG_NOTICE([Detected Avahi, using it for mDNS/DNS-SD])
      if test x$have_avahi_06 == xyes; then
         AC_DEFINE(HAVE_AVAHI_0_6, 1, [Define if mDNS/DNS-SD implementation uses Avahi 0.6])
      else
         AC_DEFINE(HAVE_AVAHI_0_5, 1, [Define if mDNS/DNS-SD implementation uses Avahi 0.5])
      fi

      have_mdns=yes
   elif test x"$have_howl" = xyes; then
      MDNS_CFLAGS=$HOWL_CFLAGS
      MDNS_LIBS=$HOWL_LIBS
      AC_DEFINE(WITH_HOWL, 1, [Define if mDNS/DNS-SD implementation uses Howl])
      AC_MSG_NOTICE([Detected Howl, using it for mDNS/DNS-SD])

      have_mdns=yes
   fi
fi

if test x"$with_mdns" = xhowl; then
   if test x"$have_howl" = xno; then
      AC_MSG_ERROR([Howl explicitly requested but not found.  Install Howl or try --with-mdns=avahi])
   fi

   MDNS_CFLAGS=$HOWL_CFLAGS
   MDNS_LIBS=$HOWL_LIBS
   AC_DEFINE(WITH_HOWL, 1, [Define if mDNS/DNS-SD implementation uses Howl])
   AC_MSG_NOTICE([Using Howl for mDNS/DNS-SD])

   have_mdns=yes
fi

if test x"$with_mdns" = xavahi; then
   if test x"$have_avahi" = xno; then
      AC_MSG_ERROR([Avahi explicitly requested but not found.  Install Avahi or try --with-mdns=howl])
   fi

   MDNS_CFLAGS=$AVAHI_CFLAGS
   MDNS_LIBS=$AVAHI_LIBS
   AC_DEFINE(WITH_AVAHI, 1, [Define if mDNS/DNS-SD implementation uses Avahi])
   AC_MSG_NOTICE([Using Avahi for mDNS/DNS-SD])

   if test x$have_avahi_06 == xyes; then
      AC_DEFINE(HAVE_AVAHI_0_6, 1, [Define if mDNS/DNS-SD implementation uses Avahi 0.6])
   else
      AC_DEFINE(HAVE_AVAHI_0_5, 1, [Define if mDNS/DNS-SD implementation uses Avahi 0.5])
   fi

   have_mdns=yes
fi



LIBNOTIFY_REQUIRED=0.2.2
AC_ARG_ENABLE(libnotify,
            AC_HELP_STRING([--disable-libnotify],
			   [Disable libnotify support]),,
	      enable_libnotify=auto)
if test "x$enable_libnotify" != "xno"; then
	PKG_CHECK_MODULES(NOTIFY,                            \
			  libnotify >= $LIBNOTIFY_REQUIRED,
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

dnl Check for libsoup, needed for DAAP and audioscrobbler
if test "x$enable_daap" = "xyes" || test "x$enable_audioscrobbler" != "xno"; then
	dnl We want libsoup now
	PKG_CHECK_MODULES(SOUP,                            \
		libsoup-2.2,
		have_libsoup=yes,
		have_libsoup=no)
	if test x"$have_libsoup" = "xno"; then
		PKG_CHECK_MODULES(SOUP,
			libsoup-2.4,
			have_libsoup=yes,
			have_libsoup=no)
	fi
fi
AM_CONDITIONAL(USE_LIBSOUP, test x"$have_libsoup" = "xyes")


dnl audioscrobbler support
if test "x$enable_audioscrobbler" != "xno"; then
	if test x"$have_libsoup" = "xno"; then
		if test "x$enable_audioscrobbler" = "xyes"; then
			AC_MSG_ERROR([AudioScrobbler support explicitly requested, but no libsoup found.  Install libsoup])
		fi
		enable_audioscrobbler=no
	else
		AC_DEFINE(WITH_AUDIOSCROBBLER, 1, [define if Audioscrobbler support should be enabled])
	fi
fi
AM_CONDITIONAL(WITH_AUDIOSCROBBLER, test "x$enable_audioscrobbler" != "xno")


dnl daap support
if test "x$enable_daap" != "xno"; then
	if test x"$have_libsoup" = "xno"; then
		if test "x$enable_daap" = "xyes"; then
			AC_MSG_ERROR([DAAP support explicitly requested, but no libsoup found.  Install libsoup])
		fi
		enable_daap=no
	elif test x"$have_mdns" = xno; then
		if test "x$enable_daap" = "xyes"; then
			AC_MSG_ERROR([DAAP support explicitly requested, but no mDNS implentation found.  Install Howl or Avahi])
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



dnl check for MusicBrainz
AC_ARG_ENABLE(musicbrainz, AC_HELP_STRING([--disable-musicbrainz],
				[don't build with MusicBrainz support]))
if test x"$enable_musicbrainz" != "xno"; then
	PKG_CHECK_MODULES(MUSICBRAINZ, libmusicbrainz >= 2.1.0, enable_musicbrainz=yes,
		          enable_musicbrainz=no)
fi
if test x"$enable_musicbrainz" = xyes; then
	RHYTHMBOX_CFLAGS="$RHYTHMBOX_CFLAGS $MUSICBRAINZ_CFLAGS"
	RHYTHMBOX_LIBS="$RHYTHMBOX_LIBS $MUSICBRAINZ_LIBS"
	AC_DEFINE(HAVE_MUSICBRAINZ, 1, [define if you have Musicbrainz])
fi
AM_CONDITIONAL(HAVE_MUSICBRAINZ, test "x$enable_musicbrainz" = "xyes")



AC_PATH_X

if test x"$x_includes" != x"NONE" && test -n "$x_includes" ; then
	CFLAGS=$CFLAGS -I`echo $x_includes | sed -e "s/:/ -I/g"`
fi
if test x"$x_libraries" != x"NONE" && test -n "$x_libraries" ; then
	LIBS=-L`echo $x_libraries | sed -e "s/:/ -L/g"`
fi

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
AC_DEFINE(WITH_AUDIOCD_SUPPORT, 1, [Define if audio cd support is enabled])
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
AM_CONDITIONAL(HAVE_LINUX_CDROM, test "x$enable_linux_cdrom" = "xyes")

NCB_DRIVE_DOOR_VERSION=2.11.3
dnl CD burner support
PKG_CHECK_MODULES(LIBNAUTILUS_BURN, [libnautilus-burn >= $NCB_MIN_REQS], have_cd_burner=yes, have_cd_burner=no)
if test "x$have_cd_burner" = xno; then
   AC_MSG_ERROR([libnautilus-burn not found or too cold])	   
fi
AC_CHECK_LIB(nautilus-burn, nautilus_burn_drive_door_is_open,
    [AC_DEFINE([HAVE_BURN_DRIVE_DOOR], 1, [Have nautilus-burn function to check drive door state])],
    ,[$LIBNAUTILUS_BURN_LIBS $LIBNAUTILUS_BURN_CFLAGS])
AC_CHECK_LIB(nautilus-burn, nautilus_burn_drive_unref,
    [AC_DEFINE([HAVE_BURN_DRIVE_UNREF], 1, [Have nautilus-burn 2.13 drive unref function])],
    ,[$LIBNAUTILUS_BURN_LIBS $LIBNAUTILUS_BURN_CFLAGS])

AC_SUBST(CFLAGS)
AC_SUBST(LDFLAGS)

AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)
AC_PATH_PROG(GCONFTOOL, gconftool-2)

AC_SUBST(RHYTHMBOX_CFLAGS)
AC_SUBST(RHYTHMBOX_LIBS)

ALL_LINGUAS="am ar az be bg ca cs cy da de el en_CA en_GB es et eu fi fr ga gl he hr hu id is it ja ko lt lv mk ml mn ms nb ne nl no pa pl pt pt_BR ro ru rw sk sr sr@Latn sv th tr uk vi zh_CN zh_TW"

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

AS_AC_EXPAND(DATADIR, $datadir)

AC_MSG_CHECKING([for D-BUS])
AC_ARG_WITH(dbus,
            AC_HELP_STRING([--with-dbus],
			   [Enable D-BUS support]),,
	      with_dbus=auto)
if test "x$with_dbus" != "xno"; then
	PKG_CHECK_MODULES(DBUS, dbus-glib-1 >= $DBUS_MIN_REQS, enable_dbus=yes, enable_dbus=no)
        if test "x$with_dbus" = xyes && test "x$enable_dbus" = xno; then
	   AC_MSG_ERROR([D-BUS explicitly requested but no support found])	   
	fi
        if test "x$enable_dbus" = xyes; then
		AC_DEFINE(WITH_DBUS, 1, [Define if D-BUS is enabled])	   
		DBUS_CFLAGS="$DBUS_CFLAGS -DDBUS_API_SUBJECT_TO_CHANGE"
		AC_MSG_RESULT([we're getting on D-BUS])

		DBUS_GLIB_BIN="`$PKG_CONFIG --variable=exec_prefix dbus-glib-1`/bin"
		AC_SUBST(DBUS_GLIB_BIN)
	else
		AC_MSG_RESULT([not found])
	fi

fi			  
AM_CONDITIONAL(WITH_DBUS, test "x$enable_dbus" = "xyes")

AC_MSG_CHECKING([for Bonobo])
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
		AC_MSG_RESULT([found])
	else
		AC_MSG_RESULT([not found])
	fi
	
fi			  
AM_CONDITIONAL(WITH_BONOBO, test "x$enable_bonobo" = "xyes")
	      
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

dnl Enable gtk-doc
GTK_DOC_CHECK(1.4)

AC_OUTPUT([ 
Makefile
macros/Makefile
lib/Makefile
metadata/Makefile
player/Makefile
rhythmdb/Makefile
widgets/Makefile
iradio/Makefile
podcast/Makefile
remote/Makefile
remote/bonobo/Makefile
remote/dbus/Makefile
shell/Makefile
data/Makefile
data/ui/Makefile
data/art/Makefile
data/glade/Makefile
data/rhythmbox.pc
sources/Makefile
daapsharing/Makefile
help/Makefile
help/C/Makefile
help/ja/Makefile
po/Makefile.in
tests/Makefile
doc/Makefile
doc/reference/Makefile
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


if test x"$enable_tag_writing" = xyes; then
        AC_MSG_NOTICE([** Tag writing is enabled - USE AT YOUR OWN RISK])
else
	AC_MSG_NOTICE([   Tag writing is disabled])
fi
if test x"$enable_mmkeys" != "xyes"; then
	AC_MSG_NOTICE([   Multimedia keys support is disabled])
else
	AC_MSG_NOTICE([** Multimedia keys support is enabled])
fi
if test x"$enable_musicbrainz" != "xyes"; then
	AC_MSG_NOTICE([   MusicBrainz support is disabled])
else
	AC_MSG_NOTICE([** MusicBrainz support is enabled])
fi

if test x"$with_playback" = xgstreamer_0_8; then
	AC_MSG_NOTICE([** GStreamer 0.8 player is enabled])
elif test x"$with_playback" = xgstreamer_0_10; then
	AC_MSG_NOTICE([** GStreamer 0.10 player is enabled])
else
	AC_MSG_ERROR([   Playback engine set incorrectly])
fi

if test x"$use_ipod" = xyes; then
	AC_MSG_NOTICE([** iPod integration enabled])
else
	AC_MSG_NOTICE([   iPod integration disabled])
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
if test x"$enable_bonobo" = xyes; then
	AC_MSG_NOTICE([** Bonobo control/activation enabled])
else
	AC_MSG_NOTICE([   Bonobo control/activation disabled])
fi
if test x"$enable_hal" = xyes; then
	AC_MSG_NOTICE([** HAL support enabled])
else
	AC_MSG_NOTICE([   HAL support disabled])
fi
if test x"$enable_dbus" = xyes; then
	AC_MSG_NOTICE([** D-BUS control/activation enabled])
else
	AC_MSG_NOTICE([   D-BUS control/activation disabled])
fi
if test x"$enable_audioscrobbler" != xno; then
	AC_MSG_NOTICE([** Audioscrobbler support enabled])
else
	AC_MSG_NOTICE([   Audioscrobbler support disabled])
fi

AC_MSG_NOTICE([End options])
