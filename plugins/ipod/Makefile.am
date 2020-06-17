plugindir = $(PLUGINDIR)/ipod
plugindatadir = $(PLUGINDATADIR)/ipod
plugin_LTLIBRARIES = libipod.la

libipod_la_SOURCES = \
	ipod-resources.c			\
	rb-ipod-db.c				\
	rb-ipod-db.h				\
	rb-ipod-helpers.c			\
	rb-ipod-helpers.h			\
	rb-ipod-static-playlist-source.c	\
	rb-ipod-static-playlist-source.h	\
	rb-ipod-source.c			\
	rb-ipod-source.h			\
	rb-ipod-plugin.c

libipod_la_LDFLAGS = $(PLUGIN_LIBTOOL_FLAGS)
libipod_la_LIBTOOLFLAGS = --tag=disable-static
libipod_la_LIBADD = 					\
	$(top_builddir)/shell/librhythmbox-core.la	\
	$(IPOD_LIBS)

AM_CPPFLAGS = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib                             \
	-I$(top_srcdir)/lib/libmediaplayerid            \
	-I$(top_srcdir)/metadata                       	\
	-I$(top_srcdir)/rhythmdb                       	\
	-I$(top_srcdir)/widgets                    	\
	-I$(top_srcdir)/sources                    	\
	-I$(top_srcdir)/sources/sync                   	\
	-I$(top_srcdir)/podcast                    	\
	-I$(top_srcdir)/shell				\
	-I$(top_srcdir)/plugins				\
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	$(RHYTHMBOX_CFLAGS)				\
	$(IPOD_CFLAGS)

if HAVE_CHECK
noinst_PROGRAMS = test-ipod
TESTS = test-ipod

test_ipod_SOURCES = 					\
	test-ipod.c					\
	rb-ipod-helpers.c				\
	rb-ipod-helpers.h

test_ipod_LDADD = \
	$(CHECK_LIBS)					\
	$(RHYTHMBOX_LIBS)				\
	$(top_builddir)/shell/librhythmbox-core.la	\
	$(IPOD_LIBS)
endif

plugin_in_files = ipod.plugin.in

%.plugin: %.plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

plugin_DATA = $(plugin_in_files:.plugin.in=.plugin)

PLUGIN_RESOURCES =					\
	ipod-info.ui	 				\
	ipod-init.ui					\
	ipod-toolbar.ui

plugin_resource_deps = $(shell $(GLIB_COMPILE_RESOURCES) --generate-dependencies --sourcedir $(srcdir) $(srcdir)/ipod.gresource.xml)
ipod-resources.c: $(plugin_resource_deps) $(srcdir)/ipod.gresource.xml
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES)		\
		--target="$@"				\
		--sourcedir="$(srcdir)"			\
		--sourcedir="$(builddir)"		\
		--generate-source "$(srcdir)/ipod.gresource.xml"

BUILT_SOURCES = ipod-resources.c $(plugin_DATA)

EXTRA_DIST = $(PLUGIN_RESOURCES) $(plugin_in_files) ipod.gresource.xml

CLEANFILES = $(BUILT_SOURCES)
DISTCLEANFILES = $(BUILT_SOURCES)
