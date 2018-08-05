plugindir = $(PLUGINDIR)/android
plugindatadir = $(PLUGINDATADIR)/android
plugin_LTLIBRARIES = libandroid.la

libandroid_la_SOURCES =					\
	android-resources.c				\
	rb-android-plugin.c				\
	rb-android-source.c				\
	rb-android-source.h

libandroid_la_LIBTOOLFLAGS = --tag=disable-static
libandroid_la_LDFLAGS = 				\
	$(top_builddir)/shell/librhythmbox-core.la	\
	$(GUDEV_LIBS)					\
	$(PLUGIN_LIBTOOL_FLAGS)

AM_CPPFLAGS = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib                        	\
	-I$(top_srcdir)/lib/libmediaplayerid           	\
	-I$(top_srcdir)/metadata                       	\
	-I$(top_srcdir)/rhythmdb                       	\
	-I$(top_srcdir)/widgets                    	\
	-I$(top_srcdir)/sources                    	\
	-I$(top_srcdir)/sources/sync                   	\
	-I$(top_srcdir)/podcast                    	\
	-I$(top_srcdir)/plugins				\
	-I$(top_srcdir)/backends			\
	-I$(top_srcdir)/shell				\
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	$(GUDEV_CFLAGS)					\
	$(RHYTHMBOX_CFLAGS)

PLUGIN_RESOURCES = android-info.ui android-toolbar.ui android.mpi

plugin_in_files = android.plugin.in

%.plugin: %.plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

plugin_DATA = $(plugin_in_files:.plugin.in=.plugin)

plugin_resource_deps = $(shell $(GLIB_COMPILE_RESOURCES) --generate-dependencies --sourcedir $(srcdir) $(srcdir)/android.gresource.xml)
android-resources.c: $(plugin_resource_deps) $(srcdir)/android.gresource.xml
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES)		\
		--target="$@"				\
		--sourcedir="$(srcdir)"			\
		--sourcedir="$(builddir)"		\
		--generate-source "$(srcdir)/android.gresource.xml"

BUILT_SOURCES = android-resources.c

EXTRA_DIST = $(PLUGIN_RESOURCES) $(plugin_in_files) android.gresource.xml

CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)

