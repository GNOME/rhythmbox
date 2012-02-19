plugindir = $(PLUGINDIR)/rblirc
plugindatadir = $(PLUGINDATADIR)/rblirc
plugin_LTLIBRARIES = librblirc.la

librblirc_la_SOURCES = \
	rb-lirc-plugin.c

librblirc_la_LDFLAGS = $(PLUGIN_LIBTOOL_FLAGS) -llirc_client
librblirc_la_LIBTOOLFLAGS = --tag=disable-static

librblirc_la_LIBADD = $(top_builddir)/shell/librhythmbox-core.la

configdir = $(plugindatadir)
config_DATA = rhythmbox_lirc_default

INCLUDES = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib                        	\
	-I$(top_srcdir)/metadata                       	\
	-I$(top_srcdir)/rhythmdb                       	\
	-I$(top_srcdir)/widgets                    	\
	-I$(top_srcdir)/iradio                    	\
	-I$(top_srcdir)/plugins				\
	-I$(top_srcdir)/shell				\
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	$(RHYTHMBOX_CFLAGS)				\
	-D_BSD_SOURCE

plugin_in_files = rblirc.plugin.in

%.plugin: %.plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

plugin_DATA = $(plugin_in_files:.plugin.in=.plugin)

EXTRA_DIST = $(plugin_in_files) $(config_DATA)

CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)
