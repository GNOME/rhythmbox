NULL =

plugindir = $(PLUGINDIR)/audioscrobbler
plugin_LTLIBRARIES = libaudioscrobbler.la

libaudioscrobbler_la_SOURCES = \
	rb-audioscrobbler-plugin.c			\
	rb-audioscrobbler-entry.h			\
	rb-audioscrobbler-entry.c			\
	rb-audioscrobbler.c				\
	rb-audioscrobbler.h				\
	rb-lastfm-source.c				\
	rb-lastfm-source.h				\
	rb-lastfm-play-order.c				\
	rb-lastfm-play-order.h

libaudioscrobbler_la_LDFLAGS = $(PLUGIN_LIBTOOL_FLAGS)
libaudioscrobbler_la_LIBADD = 				\
	$(top_builddir)/corelib/librhythmbox-core.la	\
	$(TOTEM_PLPARSER_LIBS)

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
	$(TOTEM_PLPARSER_CFLAGS)			\
	$(RHYTHMBOX_CFLAGS)				\
	-D_XOPEN_SOURCE -D_BSD_SOURCE

gladedir = $(plugindir)
glade_DATA = audioscrobbler-prefs.glade

uixmldir = $(plugindir)
uixml_DATA = audioscrobbler-ui.xml

plugin_in_files = audioscrobbler.rb-plugin.in

%.rb-plugin: %.rb-plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

BUILT_SOURCES =							\
	$(plugin_in_files:.rb-plugin.in=.rb-plugin) 		\
	$(NULL)

plugin_DATA = 					\
	$(BUILT_SOURCES)			\
	$(top_srcdir)/plugins/audioscrobbler/as-icon.png		\
	$(NULL)

EXTRA_DIST = 					\
	$(plugin_in_files)			\
	$(uixml_DATA)				\
	$(glade_DATA)				\
	$(top_srcdir)/plugins/audioscrobbler/as-icon.png		\
	$(top_srcdir)/plugins/audioscrobbler/as-powered.png		\
	$(NULL)

CLEANFILES = 			\
	$(BUILT_SOURCES)	\
	$(NULL)

DISTCLEANFILES =		\
	$(BUILT_SOURCES)	\
	$(NULL)
