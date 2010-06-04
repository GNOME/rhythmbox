NULL =

plugindir = $(PLUGINDIR)/mtpdevice
plugin_LTLIBRARIES = libmtpdevice.la

libmtpdevice_la_SOURCES = \
	rb-mtp-plugin.c	\
	rb-mtp-gst-src.c \
	rb-mtp-gst-sink.c \
	rb-mtp-source.c	\
	rb-mtp-source.h \
	rb-mtp-thread.c \
	rb-mtp-thread.h
	
libmtpdevice_la_LDFLAGS = $(PLUGIN_LIBTOOL_FLAGS)
libmtpdevice_la_LIBTOOLFLAGS = --tag=disable-static
libmtpdevice_la_LIBADD = 				\
	$(top_builddir)/shell/librhythmbox-core.la	\
	$(GUDEV_LIBS)					\
	$(HAL_LIBS)					\
	$(MTP_LIBS)

INCLUDES = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/backends			\
	-I$(top_srcdir)/lib                        	\
	-I$(top_srcdir)/lib/libmediaplayerid          	\
	-I$(top_srcdir)/metadata                       	\
	-I$(top_srcdir)/rhythmdb                       	\
	-I$(top_srcdir)/widgets                    	\
	-I$(top_srcdir)/sources                    	\
	-I$(top_srcdir)/sources/sync                   	\
	-I$(top_srcdir)/podcast                    	\
	-I$(top_srcdir)/shell				\
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	$(RHYTHMBOX_CFLAGS)				\
	$(GUDEV_CFLAGS)					\
	$(HAL_CFLAGS)					\
	$(MTP_CFLAGS)					\
	-D_XOPEN_SOURCE -D_BSD_SOURCE

plugin_in_files = mtpdevice.rb-plugin.in

%.rb-plugin: %.rb-plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

uixmldir = $(plugindir)
uixml_DATA = mtp-ui.xml

plugin_DATA = $(plugin_in_files:.rb-plugin.in=.rb-plugin)

gtkbuilderdir = $(plugindir)
gtkbuilder_DATA = 					\
	mtp-info.ui

EXTRA_DIST = $(uixml_DATA) $(plugin_in_files) $(gtkbuilder_DATA)

CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)
