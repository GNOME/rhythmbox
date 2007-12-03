## arch-tag: Automake rules for podcast library

noinst_LTLIBRARIES = librbpodcast.la librbpodcast_parse.la

librbpodcast_parse_la_SOURCES = 			\
	rb-podcast-parse.c				\
	rb-podcast-parse.h

librbpodcast_parse_la_LIBADD =				\
	$(top_builddir)/lib/librb.la

librbpodcast_la_SOURCES =				\
	rb-feed-podcast-properties-dialog.c		\
	rb-feed-podcast-properties-dialog.h		\
	rb-podcast-properties-dialog.c			\
	rb-podcast-properties-dialog.h			\
	rb-podcast-parse.c				\
	rb-podcast-parse.h				\
	rb-podcast-manager.c				\
	rb-podcast-manager.h
librbpodcast_la_LIBADD =				\
	librbpodcast_parse.la

noinst_PROGRAMS = test-podcast-parse
test_podcast_parse_SOURCES =				\
	test-podcast-parse.c
test_podcast_parse_LDADD =				\
	librbpodcast_parse.la				\
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
	-I$(top_srcdir)/library				\
	-I$(top_builddir)/lib 				\
	$(RHYTHMBOX_CFLAGS)				\
	$(TOTEM_PLPARSER_CFLAGS)

librbpodcast_la_LDFLAGS = -export-dynamic

if ENABLE_BROWSER_PLUGIN
plugindir = ${libdir}/mozilla/plugins
plugin_LTLIBRARIES = librhythmbox-itms-detection-plugin.la
librhythmbox_itms_detection_plugin_la_SOURCES = rhythmbox-itms-plugin.cpp
librhythmbox_itms_detection_plugin_la_CXXFLAGS = $(BROWSER_PLUGIN_CFLAGS)
librhythmbox_itms_detection_plugin_la_LIBADD = $(BROWSER_PLUGIN_LIBS)

librhythmbox_itms_detection_plugin_la_LDFLAGS = \
	-avoid-version	\
	-export-symbols $(srcdir)/plugin.symbols \
	-module		\
	$(AM_LDFLAGS)

endif

EXTRA_DIST = rhythmbox-itms-plugin.cpp plugin.symbols

