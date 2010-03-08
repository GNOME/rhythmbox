NULL =

plugindir = $(PLUGINDIR)/status-icon
plugin_LTLIBRARIES = libstatus-icon.la

ICON_IMPL_FILES = \
	rb-tray-icon-gtk.h 				\
	rb-tray-icon-gtk.c

libstatus_icon_la_SOURCES = \
	rb-status-icon-plugin.h				\
	rb-status-icon-plugin.c				\
	$(ICON_IMPL_FILES)				\
	$(NULL)


libstatus_icon_la_LDFLAGS = $(PLUGIN_LIBTOOL_FLAGS)
libstatus_icon_la_LIBTOOLFLAGS = --tag=disable-static

libstatus_icon_la_LIBADD = 				\
	$(top_builddir)/shell/librhythmbox-core.la	\
	$(NOTIFY_LIBS)					\
	$(NULL)

INCLUDES = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib                        	\
	-I$(top_srcdir)/rhythmdb                       	\
	-I$(top_srcdir)/widgets                    	\
	-I$(top_srcdir)/sources                    	\
	-I$(top_srcdir)/plugins				\
	-I$(top_srcdir)/shell				\
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	$(RHYTHMBOX_CFLAGS)				\
	-D_XOPEN_SOURCE -D_BSD_SOURCE

uixmldir = $(plugindir)
uixml_DATA = 						\
	status-icon-ui.xml				\
	status-icon-preferences.ui			\
	$(NULL)

plugin_in_files = status-icon.rb-plugin.in

%.rb-plugin: %.rb-plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

BUILT_SOURCES =							\
	$(plugin_in_files:.rb-plugin.in=.rb-plugin) 	\
	$(NULL)

plugin_DATA = 			\
	$(BUILT_SOURCES)	\
	$(NULL)

EXTRA_DIST = 			\
	$(glade_DATA)		\
	$(uixml_DATA)		\
	$(plugin_in_files)	\
	$(NULL)

CLEANFILES = 			\
	$(BUILT_SOURCES)	\
	$(NULL)

DISTCLEANFILES =		\
	$(BUILT_SOURCES)	\
	$(NULL)

