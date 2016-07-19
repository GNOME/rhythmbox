NULL =

plugindir = $(PLUGINDIR)/iradio
plugindatadir = $(PLUGINDATADIR)/iradio
plugin_LTLIBRARIES = libiradio.la

libiradio_la_SOURCES = 					\
	iradio-resources.c				\
	rb-iradio-plugin.c				\
	rb-iradio-source.c				\
	rb-iradio-source.h				\
	rb-iradio-source-search.c			\
	rb-iradio-source-search.h			\
	rb-station-properties-dialog.c			\
	rb-station-properties-dialog.h

libiradio_la_LDFLAGS = $(PLUGIN_LIBTOOL_FLAGS)
libiradio_la_LIBTOOLFLAGS = --tag=disable-static
libiradio_la_LIBADD = 					\
	$(top_builddir)/shell/librhythmbox-core.la	\
	$(TOTEM_PLPARSER_LIBS)				\
	$(NULL)

AM_CPPFLAGS = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib                        	\
	-I$(top_srcdir)/metadata                       	\
	-I$(top_srcdir)/rhythmdb                       	\
	-I$(top_srcdir)/widgets                    	\
	-I$(top_srcdir)/sources                    	\
	-I$(top_srcdir)/plugins				\
	-I$(top_srcdir)/shell				\
	-I$(top_srcdir)/backends			\
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	$(RHYTHMBOX_CFLAGS)				\
	$(TOTEM_PLPARSER_CFLAGS)

PLUGIN_RESOURCES = 					\
	iradio-initial.xspf				\
	iradio-popup.ui					\
	iradio-toolbar.ui				\
	station-properties.ui

plugin_resource_deps = $(shell $(GLIB_COMPILE_RESOURCES) --generate-dependencies --sourcedir $(srcdir) $(srcdir)/iradio.gresource.xml)
iradio-resources.c: $(plugin_resource_deps) $(srcdir)/iradio.gresource.xml
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES)		\
		--target="$@"				\
		--sourcedir="$(srcdir)"			\
		--sourcedir="$(builddir)"		\
		--generate-source "$(srcdir)/iradio.gresource.xml"

BUILT_SOURCES = iradio-resources.c

plugin_in_files = iradio.plugin.in

%.plugin: %.plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

plugin_DATA = $(plugin_in_files:.plugin.in=.plugin)
BUILT_SOURCES += $(plugin_DATA)

EXTRA_DIST = 			\
	$(PLUGIN_RESOURCES)	\
	$(plugin_in_files)	\
	iradio.gresource.xml	\
	$(NULL)

CLEANFILES = 			\
	$(BUILT_SOURCES)	\
	$(NULL)

DISTCLEANFILES =		\
	$(BUILT_SOURCES)	\
	$(NULL)

