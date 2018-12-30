plugindir = $(SAMPLEPLUGINDIR)/sample-vala
plugin_LTLIBRARIES = libsample-vala.la

libsample_vala_la_SOURCES = \
	rb-sample-vala-plugin.vala
	
libsample_vala_la_LDFLAGS = $(PLUGIN_LIBTOOL_FLAGS)

# this causes automake to barf confusingly
# libsample_vala_la_LIBTOOLFLAGS = --tag=disable-static

libsample_vala_la_CFLAGS = $(WNOERROR_CFLAGS)

AM_CPPFLAGS = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib                        	\
	-I$(top_srcdir)/metadata                       	\
	-I$(top_srcdir)/rhythmdb                       	\
	-I$(top_srcdir)/widgets                    	\
	-I$(top_srcdir)/sources                    	\
	-I$(top_srcdir)/podcast                    	\
	-I$(top_srcdir)/plugins				\
	-I$(top_srcdir)/shell				\
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	$(RHYTHMBOX_CFLAGS)


plugin_in_files = sample-vala.plugin.in

%.plugin: %.plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po); $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

AM_VALAFLAGS = --vapidir=$(top_srcdir)/bindings/vala --pkg=rhythmdb --pkg=rb --pkg=gstreamer-1.0 --pkg=libpeas-1.0

%.c %.h: %.vala
	$(VALAC) -C $(AM_VALAFLAGS) $^
	
plugin_DATA = $(plugin_in_files:.plugin.in=.plugin)

EXTRA_DIST = $(plugin_in_files)

CLEANFILES = $(plugin_DATA) $(libsample_vala_la_SOURCES:.vala=.h) $(libsample_vala_la_SOURCES:.vala=.c)
DISTCLEANFILES = $(CLEANFILES)
