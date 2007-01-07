NULL =

plugindir = $(PLUGINDIR)/iradio
plugin_LTLIBRARIES = libiradio.la

libiradio_la_SOURCES = \
	rb-iradio-plugin.c					\
	rb-iradio-source.c		\
	rb-iradio-source.h		\
	rb-station-properties-dialog.c	\
	rb-station-properties-dialog.h

libiradio_la_LDFLAGS = $(PLUGIN_LIBTOOL_FLAGS)
libiradio_la_LIBADD = 					\
	$(top_builddir)/corelib/librhythmbox-core.la

INCLUDES = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib                        	\
	-I$(top_srcdir)/metadata                       	\
	-I$(top_srcdir)/rhythmdb                       	\
	-I$(top_srcdir)/widgets                    	\
	-I$(top_srcdir)/sources                    	\
	-I$(top_srcdir)/plugins				\
	-I$(top_srcdir)/shell				\
	-I$(top_srcdir)/backends			\
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	$(RHYTHMBOX_CFLAGS)				\
	$(WARN_CFLAGS)					\
	$(TOTEM_PLPARSER_CFLAGS)			\
	-D_XOPEN_SOURCE -D_BSD_SOURCE

gladedir = $(plugindir)
glade_DATA = \
	station-properties.glade

uixmldir = $(plugindir)
uixml_DATA = iradio-ui.xml

plugin_in_files = iradio.rb-plugin.desktop.in

%.rb-plugin: %.rb-plugin.desktop.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

BUILT_SOURCES =							\
	$(plugin_in_files:.rb-plugin.desktop.in=.rb-plugin) 	\
	$(NULL)

plugin_DATA = 			\
	$(BUILT_SOURCES)	\
	iradio-initial.pls	\
	$(NULL)

EXTRA_DIST = 			\
	$(glade_DATA)		\
	$(uixml_DATA)		\
	$(plugin_in_files)	\
	iradio-initial.pls	\
	$(NULL)

CLEANFILES = 			\
	$(BUILT_SOURCES)	\
	$(NULL)

DISTCLEANFILES =		\
	$(BUILT_SOURCES)	\
	$(NULL)

