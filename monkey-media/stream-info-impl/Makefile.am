SUBDIRS = id3-vfs

noinst_LTLIBRARIES = libstream-info-impl.la

libstream_info_impl_la_SOURCES =
libstream_info_impl_la_LIBADD =

if HAVE_VORBIS
libstream_info_impl_la_SOURCES += vorbis-stream-info-impl.c vorbis-stream-info-impl.h \
				  ogg-helper.c ogg-helper.h
endif

if HAVE_FLAC
libstream_info_impl_la_SOURCES += flac-stream-info-impl.c flac-stream-info-impl.h
endif

if HAVE_AUDIOCD
libstream_info_impl_la_SOURCES += audiocd-stream-info-impl.c audiocd-stream-info-impl.h 
endif

if HAVE_MP3
libstream_info_impl_la_SOURCES += mp3-stream-info-impl.c mp3-stream-info-impl.h 
libstream_info_impl_la_LIBADD += $(top_builddir)/monkey-media/stream-info-impl/id3-vfs/libid3-vfs.la
endif


INCLUDES = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"MonkeyMedia\"			\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/monkey-media			\
	$(RHYTHMBOX_CFLAGS)

EXTRA_DIST = flac-stream-info-impl.c flac-stream-info-impl.h
