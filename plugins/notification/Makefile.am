NULL =

plugindir = $(PLUGINDIR)/notification
plugin_LTLIBRARIES = libnotification.la

libnotification_la_SOURCES = \
	rb-notification-plugin.c
	$(NULL)

libnotification_la_LDFLAGS = $(PLUGIN_LIBTOOL_FLAGS)
libnotification_la_LIBTOOLFLAGS = --tag=disable-static

libnotification_la_LIBADD = 				\
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
	-I$(top_srcdir)/shell				\
	-I$(top_srcdir)/plugins				\
	-I$(top_srcdir)/metadata			\
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	$(RHYTHMBOX_CFLAGS)				\
	$(NOTIFY_CFLAGS)				\
	-D_BSD_SOURCE

plugin_in_files = notification.plugin.in

%.plugin: %.plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

BUILT_SOURCES =					\
	$(plugin_in_files:.plugin.in=.plugin) 	\
	$(NULL)

plugin_DATA = 			\
	$(BUILT_SOURCES)	\
	$(NULL)

EXTRA_DIST = 			\
	$(plugin_in_files)	\
	$(NULL)

CLEANFILES = 			\
	$(BUILT_SOURCES)	\
	$(NULL)

DISTCLEANFILES =		\
	$(BUILT_SOURCES)	\
	$(NULL)
