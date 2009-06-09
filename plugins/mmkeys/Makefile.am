NULL =

plugindir = $(PLUGINDIR)/mmkeys
plugin_LTLIBRARIES = libmmkeys.la

libmmkeys_la_SOURCES = \
	rb-mmkeys-plugin.c				\
	$(NULL)

libmmkeys_la_LDFLAGS = $(PLUGIN_LIBTOOL_FLAGS)
libmmkeys_la_LIBTOOLFLAGS = --tag=disable-static

libmmkeys_la_LIBADD =					\
	$(top_builddir)/shell/librhythmbox-core.la	\
	$(DBUS_LIBS)					\
	$(NULL)

INCLUDES = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib                        	\
	-I$(top_srcdir)/player                       	\
	-I$(top_srcdir)/rhythmdb                       	\
	-I$(top_srcdir)/widgets                    	\
	-I$(top_srcdir)/sources                    	\
	-I$(top_srcdir)/plugins				\
	-I$(top_srcdir)/shell				\
	-I$(top_builddir)/lib   			\
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	$(RHYTHMBOX_CFLAGS)				\
	$(DBUS_CFLAGS)					\
	-D_XOPEN_SOURCE -D_BSD_SOURCE			\
	$(NULL)

plugin_in_files = mmkeys.rb-plugin.in

%.rb-plugin: %.rb-plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

plugin_DATA = $(plugin_in_files:.rb-plugin.in=.rb-plugin)

EXTRA_DIST = $(plugin_in_files)

CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)
