## arch-tag: Automake rules for source object library

noinst_LTLIBRARIES = libsources.la libsourcesimpl.la

libsources_la_SOURCES = rb-source.c rb-source.h

libsourcesimpl_la_SOURCES =		\
	rb-sourcelist.c			\
	rb-sourcelist.h			\
	rb-sourcelist-model.c		\
	rb-sourcelist-model.h		\
	rb-library-source.c		\
	rb-library-source.h		\
	rb-iradio-source.c		\
	rb-iradio-source.h		\
	rb-playlist-source.c            \
	rb-playlist-source.h		
#	rb-audiocd-source.c		
#	rb-audiocd-source.h
#endif

INCLUDES =						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	-I$(top_builddir)/lib 				\
	-I$(top_srcdir)/rhythmdb			\
	-I$(top_srcdir)/widgets 			\
	-I$(top_srcdir)/library 			\
	-I$(top_srcdir)/iradio				\
	-I$(top_srcdir)/monkey-media			\
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	$(WARN_CFLAGS)					\
	$(RHYTHMBOX_CFLAGS)

libsources_la_LDFLAGS = -export-dynamic
libsourcesimpl_la_LDFLAGS = -export-dynamic
libsourcesimpl_la_LIBADD = $(top_builddir)/monkey-media/libmonkey-media.la $(top_builddir)/rhythmdb/librhythmdb.la $(top_builddir)/widgets/librbwidgets.la
