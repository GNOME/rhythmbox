NULL =

plugindir = $(PLUGINDIR)/visualizer
plugin_LTLIBRARIES = libvisualizer.la

libvisualizer_la_SOURCES = \
	rb-visualizer-plugin.c				\
	rb-vis-widget.c					\
	rb-vis-widget.h					\
	rb-fake-visualizer.c

libvisualizer_la_LDFLAGS = $(PLUGIN_LIBTOOL_FLAGS)

libvisualizer_la_LIBADD = 				\
	-lgstinterfaces-0.10				\
	$(DBUS_LIBS)					\
	$(RHYTHMBOX_LIBS)

INCLUDES = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib                        	\
	-I$(top_srcdir)/metadata                       	\
	-I$(top_srcdir)/backends                       	\
	-I$(top_srcdir)/backends/gstreamer            	\
	-I$(top_srcdir)/rhythmdb                       	\
	-I$(top_srcdir)/widgets                    	\
	-I$(top_srcdir)/sources                    	\
	-I$(top_srcdir)/iradio                    	\
	-I$(top_srcdir)/podcast                    	\
	-I$(top_srcdir)/remote				\
	-I$(top_builddir)/remote			\
	-I$(top_srcdir)/plugins				\
	-I$(top_srcdir)/shell				\
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	-DPLUGIN_SRC_DIR=\""$(ROOT_UNINSTALLED_DIR)/plugins/visualizer"\" \
	$(RHYTHMBOX_CFLAGS)				\
	$(WARN_CFLAGS)					\
	$(DBUS_CFLAGS)					\
	-D_XOPEN_SOURCE -D_BSD_SOURCE

gladedir = $(plugindir)
glade_DATA = visualizer-controls.glade

uixmldir = $(plugindir)
uixml_DATA = visualizer-ui.xml

themedir = $(pkgdatadir)/icons/hicolor
size = 22x22
context = actions

icondir = $(themedir)/$(size)/$(context)
icon_DATA = icons/hicolor/$(size)/$(context)/visualization.png

plugin_in_files = visualizer.rb-plugin.desktop.in

%.rb-plugin: %.rb-plugin.desktop.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

BUILT_SOURCES =							\
	$(plugin_in_files:.rb-plugin.desktop.in=.rb-plugin) 	\
	$(NULL)

plugin_DATA = 			\
	$(BUILT_SOURCES)	\
	$(NULL)

EXTRA_DIST = 			\
	$(glade_DATA)		\
	$(uixml_DATA)		\
	$(icon_DATA)		\
	$(plugin_in_files)	\
	$(NULL)

CLEANFILES = 			\
	$(BUILT_SOURCES)	\
	$(NULL)

DISTCLEANFILES =		\
	$(BUILT_SOURCES)	\
	$(NULL)

if WITH_DBUS
rb-visualizer-glue.h: rb-visualizer.xml Makefile
	$(LIBTOOL) --mode=execute $(DBUS_GLIB_BIN)/dbus-binding-tool --prefix=rb_visualizer --mode=glib-server --output=$@ $<

BUILT_SOURCES += rb-visualizer-glue.h
EXTRA_DIST += rb-visualizer.xml

libvisualizer_la_LIBADD += $(DBUS_LIBS)
endif

