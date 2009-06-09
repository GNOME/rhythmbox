## arch-tag: Automake rules for podcast library

noinst_LTLIBRARIES = librbpodcast.la librbpodcast_parse.la

librbpodcast_parse_la_SOURCES = 			\
	rb-podcast-parse.c				\
	rb-podcast-parse.h

librbpodcast_la_SOURCES =				\
	rb-feed-podcast-properties-dialog.c		\
	rb-feed-podcast-properties-dialog.h		\
	rb-podcast-properties-dialog.c			\
	rb-podcast-properties-dialog.h			\
	rb-podcast-parse.c				\
	rb-podcast-parse.h				\
	rb-podcast-manager.c				\
	rb-podcast-manager.h

noinst_PROGRAMS = test-podcast-parse
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
	-I$(top_srcdir)/library				\
	-I$(top_builddir)/lib 				\
	$(RHYTHMBOX_CFLAGS)				\
	$(TOTEM_PLPARSER_CFLAGS)

librbpodcast_la_LDFLAGS = -export-dynamic

PLUGIN_FILES = rhythmbox-itms-plugin.cpp npapi.h npupp.h npruntime.h

if ENABLE_BROWSER_PLUGIN
plugindir = $(MOZILLA_PLUGINDIR)
plugin_LTLIBRARIES = librhythmbox-itms-detection-plugin.la
librhythmbox_itms_detection_plugin_la_SOURCES = $(PLUGIN_FILES)
librhythmbox_itms_detection_plugin_la_CXXFLAGS = -I$(top_srcdir)/podcast

librhythmbox_itms_detection_plugin_la_LDFLAGS = \
	-avoid-version	\
	-export-symbols $(srcdir)/plugin.symbols \
	-module

endif

EXTRA_DIST = $(PLUGIN_FILES) plugin.symbols

