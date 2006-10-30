## arch-tag: Automake rules for source object library
noinst_LTLIBRARIES = libsources.la libsourcesimpl.la

libsources_la_SOURCES = 		\
	rb-source.c 			\
	rb-source.h			\
	rb-streaming-source.c		\
	rb-streaming-source.h

libsourcesimpl_la_SOURCES =		\
	rb-sourcelist.c			\
	rb-sourcelist.h			\
	rb-sourcelist-model.c		\
	rb-sourcelist-model.h		\
	rb-browser-source.c		\
	rb-browser-source.h		\
	rb-library-source.c		\
	rb-library-source.h		\
	rb-podcast-source.c		\
	rb-podcast-source.h		\
	rb-removable-media-source.c	\
	rb-removable-media-source.h	\
	rb-playlist-source.c            \
	rb-playlist-source.h		\
	rb-playlist-xml.h		\
	rb-auto-playlist-source.c	\
	rb-auto-playlist-source.h	\
	rb-static-playlist-source.c	\
	rb-static-playlist-source.h	\
	rb-play-queue-source.c		\
	rb-play-queue-source.h		\
	rb-missing-files-source.c	\
	rb-missing-files-source.h	\
	rb-import-errors-source.c	\
	rb-import-errors-source.h

INCLUDES =						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	-I$(top_builddir)/lib 				\
	-I$(top_srcdir)/rhythmdb			\
	-I$(top_srcdir)/metadata 			\
	-I$(top_srcdir)/widgets 			\
	-I$(top_srcdir)/library 			\
	-I$(top_srcdir)/player	 			\
	-I$(top_srcdir)/iradio				\
	-I$(top_srcdir)/podcast				\
	-I$(top_srcdir)/shell				\
	-I$(top_srcdir)/daapsharing			\
	-I$(top_srcdir)/backends			\
	-I$(top_srcdir)/plugins				\
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	$(WARN_CFLAGS)					\
	$(TOTEM_PLPARSER_CFLAGS)			\
	$(HAL_CFLAGS)					\
	$(RHYTHMBOX_CFLAGS)

libsources_la_LDFLAGS = -export-dynamic
libsourcesimpl_la_LDFLAGS = -export-dynamic

daap_files =				\
	rb-daap-source.c		\
	rb-daap-source.h
if USE_DAAP
libsourcesimpl_la_SOURCES += $(daap_files)
if USE_GNOME_KEYRING
INCLUDES +=						\
	$(GNOME_KEYRING_CFLAGS)
endif
endif

if USE_LIBSOUP
INCLUDES += $(SOUP_CFLAGS)
endif

if ENABLE_TRACK_TRANSFER
INCLUDES +=$(GNOME_MEDIA_PROFILES_CFLAGS)
endif

EXTRA_DIST = $(daap_files)
