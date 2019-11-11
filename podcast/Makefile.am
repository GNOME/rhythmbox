noinst_LTLIBRARIES = librbpodcast.la librbpodcast_parse.la

podcastincludedir = $(includedir)/rhythmbox/podcast
podcastinclude_HEADERS =				\
	rb-podcast-entry-types.h			\
	rb-podcast-parse.h				\
	rb-podcast-manager.h				\
	rb-podcast-search.h


librbpodcast_parse_la_SOURCES = 			\
	rb-podcast-parse.c				\
	rb-podcast-parse.h				\
	rb-podcast-search.c				\
	rb-podcast-search-itunes.c

librbpodcast_la_SOURCES =				\
	$(podcastinclude_HEADERS)			\
	rb-feed-podcast-properties-dialog.c		\
	rb-feed-podcast-properties-dialog.h		\
	rb-podcast-add-dialog.c				\
	rb-podcast-add-dialog.h				\
	rb-podcast-properties-dialog.c			\
	rb-podcast-properties-dialog.h			\
	rb-podcast-main-source.c			\
	rb-podcast-main-source.h			\
	rb-podcast-search.c				\
	rb-podcast-search-itunes.c			\
	rb-podcast-settings.h				\
	rb-podcast-source.c				\
	rb-podcast-source.h				\
	rb-podcast-parse.c				\
	rb-podcast-manager.c				\
	rb-podcast-entry-types.c

noinst_PROGRAMS = test-podcast-parse test-podcast-search

test_podcast_search_SOURCES =				\
	test-podcast-search.c
test_podcast_search_LDADD =				\
	librbpodcast_parse.la				\
	$(top_builddir)/lib/librb.la			\
	$(RHYTHMBOX_LIBS)				\
	$(JSON_GLIB_LIBS)				\
	$(TOTEM_PLPARSER_LIBS)


test_podcast_parse_SOURCES =				\
	test-podcast-parse.c
test_podcast_parse_LDADD =				\
	librbpodcast_parse.la				\
	$(top_builddir)/lib/librb.la			\
	$(RHYTHMBOX_LIBS)				\
	$(TOTEM_PLPARSER_LIBS)

AM_CFLAGS =						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	-I$(top_srcdir)/rhythmdb			\
	-I$(top_srcdir)/widgets				\
	-I$(top_srcdir)/shell				\
	-I$(top_srcdir)/metadata			\
	-I$(top_srcdir)/sources				\
	-I$(top_builddir)/lib 				\
	$(RHYTHMBOX_CFLAGS)				\
	$(JSON_GLIB_CFLAGS)				\
	$(TOTEM_PLPARSER_CFLAGS)

librbpodcast_la_LDFLAGS = -export-dynamic
librbpodcast_la_LIBADD = $(JSON_GLIB_LIBS)
