NULL =

plugindir = $(PLUGINDIR)/audioscrobbler
plugin_LTLIBRARIES = libaudioscrobbler.la

libaudioscrobbler_la_SOURCES = \
	rb-audioscrobbler-plugin.c			\
	rb-audioscrobbler.c				\
	rb-audioscrobbler.h				\
	rb-lastfm-source.c				\
	rb-lastfm-source.h

if USE_GSTREAMER_0_10
libaudioscrobbler_la_SOURCES += rb-lastfm-gst-src.c
endif

libaudioscrobbler_la_LDFLAGS = $(PLUGIN_LIBTOOL_FLAGS)
libaudioscrobbler_la_LIBADD = 				\
	$(top_builddir)/corelib/librhythmbox-core.la	\
	$(SOUP_LIBS)

INCLUDES = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib                        	\
	-I$(top_srcdir)/metadata                       	\
	-I$(top_srcdir)/player                       	\
	-I$(top_srcdir)/rhythmdb                       	\
	-I$(top_srcdir)/widgets                    	\
	-I$(top_srcdir)/sources                    	\
	-I$(top_srcdir)/iradio                    	\
	-I$(top_srcdir)/podcast                    	\
	-I$(top_srcdir)/plugins				\
	-I$(top_srcdir)/shell				\
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	$(SOUP_CFLAGS)					\
	$(RHYTHMBOX_CFLAGS)				\
	$(WARN_CFLAGS)					\
	-D_XOPEN_SOURCE -D_BSD_SOURCE

gladedir = $(plugindir)
glade_DATA = audioscrobbler-prefs.glade

uixmldir = $(plugindir)
uixml_DATA = audioscrobbler-ui.xml

plugin_in_files = audioscrobbler.rb-plugin.desktop.in

%.rb-plugin: %.rb-plugin.desktop.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

BUILT_SOURCES =							\
	$(plugin_in_files:.rb-plugin.desktop.in=.rb-plugin) 	\
	$(NULL)

plugin_DATA = 					\
	$(BUILT_SOURCES)			\
	$(top_srcdir)/plugins/audioscrobbler/as-icon.svg		\
	$(NULL)

EXTRA_DIST = 					\
	$(plugin_in_files)			\
	$(uixml_DATA)				\
	$(glade_DATA)				\
	$(top_srcdir)/plugins/audioscrobbler/as-powered.png		\
	$(NULL)

CLEANFILES = 			\
	$(BUILT_SOURCES)	\
	$(NULL)

DISTCLEANFILES =		\
	$(BUILT_SOURCES)	\
	$(NULL)
