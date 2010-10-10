NULL =

SUBDIRS = sync

noinst_LTLIBRARIES = libsources.la

sourceincludedir = $(includedir)/rhythmbox/sources
sourceinclude_HEADERS = 		\
	rb-source.h			\
	rb-streaming-source.h		\
	rb-source-group.h		\
	rb-source-search.h		\
	rb-sourcelist.h			\
	rb-sourcelist-model.h		\
	rb-browser-source.h		\
	rb-removable-media-source.h	\
	rb-media-player-source.h	\
	rb-playlist-source.h		\
	rb-playlist-xml.h		\
	rb-auto-playlist-source.h	\
	rb-static-playlist-source.h	\
	rb-source-search-basic.h

libsources_la_SOURCES = 		\
	$(sourceinclude_HEADERS)	\
	rb-source.c 			\
	rb-streaming-source.c		\
	rb-source-group.c		\
	rb-source-search.c		\
	rb-sourcelist.c			\
	rb-sourcelist-model.c		\
	rb-browser-source.c		\
	rb-library-source.c		\
	rb-library-source.h		\
	rb-removable-media-source.c	\
	rb-media-player-source.c	\
	rb-playlist-source.c            \
	rb-auto-playlist-source.c	\
	rb-static-playlist-source.c	\
	rb-play-queue-source.c		\
	rb-play-queue-source.h		\
	rb-missing-files-source.c	\
	rb-missing-files-source.h	\
	rb-import-errors-source.c	\
	rb-import-errors-source.h	\
	rb-source-search-basic.c	\
	$(NULL)

INCLUDES =						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	-I$(top_srcdir)/lib/libmediaplayerid		\
	-I$(top_builddir)/lib 				\
	-I$(top_srcdir)/rhythmdb			\
	-I$(top_srcdir)/metadata 			\
	-I$(top_srcdir)/widgets 			\
	-I$(top_srcdir)/shell				\
	-I$(top_srcdir)/podcast				\
	-I$(top_srcdir)/sources/sync			\
	-I$(top_srcdir)/backends			\
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	$(TOTEM_PLPARSER_CFLAGS)			\
	$(RHYTHMBOX_CFLAGS)				\
	$(NO_STRICT_ALIASING_CFLAGS)

libsources_la_LDFLAGS = -export-dynamic
