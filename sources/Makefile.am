noinst_LTLIBRARIES = libsources.la libsourcesimpl.la

libsources_la_SOURCES = rb-source.c rb-source.h

libsourcesimpl_la_SOURCES =		\
	rb-library-source.c		\
	rb-library-source.h		\
	rb-song-info-helpers.c		\
	rb-song-info-helpers.h		\
	rb-iradio-source.c		\
	rb-iradio-source.h		\
	rb-group-source.c               \
	rb-group-source.h		
#	rb-audiocd-source.c		
#	rb-audiocd-source.h
#endif

INCLUDES =						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	-I$(top_srcdir)/lib/widgets 			\
	-I$(top_srcdir)/library 			\
	-I$(top_srcdir)/iradio				\
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	$(WARN_CFLAGS)					\
	$(RHYTHMBOX_CFLAGS)

libsources_la_LDFLAGS = -export-dynamic
libsourcesimpl_la_LDFLAGS = -export-dynamic
