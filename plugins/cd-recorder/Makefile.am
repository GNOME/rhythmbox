NULL =

plugindir = $(PLUGINDIR)/cd-recorder
plugin_LTLIBRARIES = libcd-recorder.la

libcd_recorder_la_SOURCES =					\
	rb-cd-recorder-plugin.c					\
	rb-recorder.h						\
	rb-recorder-gst.c					\
	rb-playlist-source-recorder.c				\
	rb-playlist-source-recorder.h				\
	$(NULL)

libcd_recorder_la_LDFLAGS = $(PLUGIN_LIBTOOL_FLAGS)
libcd_recorder_la_LIBTOOLFLAGS = --tag=disable-static
libcd_recorder_la_LIBADD = 				\
	$(top_builddir)/shell/librhythmbox-core.la	\
	$(LIBNAUTILUS_BURN_LIBS)			\
	$(NULL)

INCLUDES = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib                        	\
	-I$(top_builddir)/lib				\
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
	$(RHYTHMBOX_CFLAGS)				\
	$(LIBNAUTILUS_BURN_CFLAGS)			\
	-D_XOPEN_SOURCE -D_BSD_SOURCE			\
	$(NULL)

plugin_in_files = cd-recorder.rb-plugin.in

%.rb-plugin: %.rb-plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

gtkbuilderdir = $(plugindir)
gtkbuilder_DATA = recorder.ui

BUILT_SOURCES =							\
	$(plugin_in_files:.rb-plugin.in=.rb-plugin) 		\
	$(NULL)

plugin_DATA = 			\
	$(BUILT_SOURCES)	\
	$(NULL)

EXTRA_DIST = 			\
	$(plugin_in_files)	\
	$(gtkbuilder_DATA)	\
	$(NULL)

CLEANFILES = 			\
	$(BUILT_SOURCES)	\
	$(NULL)

DISTCLEANFILES =		\
	$(BUILT_SOURCES)	\
	$(NULL)
