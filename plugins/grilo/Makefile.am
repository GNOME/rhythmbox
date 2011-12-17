plugindir = $(PLUGINDIR)/grilo
plugindatadir = $(PLUGINDATADIR)/grilo
plugin_LTLIBRARIES = libgrilo.la

libgrilo_la_SOURCES =					\
	rb-grilo-plugin.c				\
	rb-grilo-source.c				\
	rb-grilo-source.h

libgrilo_la_LDFLAGS = $(PLUGIN_LIBTOOL_FLAGS)
libgrilo_la_LIBTOOLFLAGS = --tag=disable-static

libgrilo_la_LIBADD = \
	$(top_builddir)/shell/librhythmbox-core.la	\
	$(GRILO_LIBS)

INCLUDES = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib                             \
	-I$(top_srcdir)/rhythmdb                       	\
	-I$(top_srcdir)/widgets                    	\
	-I$(top_srcdir)/sources                    	\
	-I$(top_srcdir)/plugins				\
	-I$(top_srcdir)/shell				\
	-I$(top_srcdir)/metadata			\
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	$(RHYTHMBOX_CFLAGS)				\
	$(GRILO_CFLAGS)					\
	-D_BSD_SOURCE

libgrilo_la_LIBADD += $(NULL)

plugin_in_files = grilo.plugin.in

%.plugin: %.plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

plugin_DATA = $(plugin_in_files:.plugin.in=.plugin)

EXTRA_DIST = $(plugin_in_files)

CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)
