NULL =

plugindir = $(PLUGINDIR)/dbus-media-server
plugin_LTLIBRARIES = libdbus-media-server.la

libdbus_media_server_la_SOURCES = \
	rb-dbus-media-server-plugin.c			\
	dbus-media-server-spec.h			\
	$(NULL)

libdbus_media_server_la_LDFLAGS = $(PLUGIN_LIBTOOL_FLAGS)
libdbus_media_server_la_LIBTOOLFLAGS = --tag=disable-static

libdbus_media_server_la_LIBADD =					\
	$(top_builddir)/shell/librhythmbox-core.la	\
	$(NULL)

INCLUDES = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_builddir)	   			\
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	$(RHYTHMBOX_CFLAGS)				\
	-D_XOPEN_SOURCE -D_BSD_SOURCE			\
	$(NULL)

plugin_in_files = dbus-media-server.rb-plugin.in

%.rb-plugin: %.rb-plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

plugin_DATA = $(plugin_in_files:.rb-plugin.in=.rb-plugin)

EXTRA_DIST = $(plugin_in_files)

CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)
