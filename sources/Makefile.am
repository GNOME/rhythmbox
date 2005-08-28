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

INCLUDES =						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	-I$(top_builddir)/lib 				\
	-I$(top_srcdir)/corba 				\
	-I$(top_builddir)/corba				\
	-I$(top_srcdir)/rhythmdb			\
	-I$(top_srcdir)/widgets 			\
	-I$(top_srcdir)/library 			\
	-I$(top_srcdir)/player	 			\
	-I$(top_srcdir)/iradio				\
	-I$(top_srcdir)/shell				\
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	$(WARN_CFLAGS)					\
	$(TOTEM_PLPARSER_CFLAGS)			\
	$(RHYTHMBOX_CFLAGS)

libsources_la_LDFLAGS = -export-dynamic
libsourcesimpl_la_LDFLAGS = -export-dynamic

libsourcesimpl_la_LIBADD = $(TOTEM_PLPARSER_LIBS) #for iradio

if USE_IPOD
libsourcesimpl_la_SOURCES +=         \
	rb-ipod-source.c             \
	rb-ipod-source.h	     \
	itunesdb.c		     \
	itunesdb.h
INCLUDES += $(HAL_CFLAGS)
libsourcesimpl_la_LIBADD += $(HAL_LIBS)
endif

if USE_CD_BURNER
libsourcesimpl_la_SOURCES += 		\
	rb-playlist-source-recorder.c   \
	rb-playlist-source-recorder.h	

INCLUDES += $(LIBNAUTILUS_BURN_CFLAGS)
endif

