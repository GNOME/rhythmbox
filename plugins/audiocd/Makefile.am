plugindir = $(PLUGINDIR)/audiocd
plugindatadir = $(PLUGINDATADIR)/audiocd
plugin_LTLIBRARIES = libaudiocd.la

libaudiocd_la_SOURCES =					\
	rb-audiocd-info.c				\
	rb-audiocd-info.h				\
	rb-audiocd-plugin.c				\
	rb-audiocd-source.c				\
	rb-audiocd-source.h				\
	rb-musicbrainz-lookup.c				\
	rb-musicbrainz-lookup.h

libaudiocd_la_LDFLAGS = $(PLUGIN_LIBTOOL_FLAGS)
libaudiocd_la_LIBTOOLFLAGS = --tag=disable-static

libaudiocd_la_LIBADD = \
	$(top_builddir)/shell/librhythmbox-core.la	\
	$(TOTEM_PLPARSER_LIBS)

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
	-I$(top_srcdir)/remote				\
	-I$(top_builddir)/remote			\
	-I$(top_srcdir)/plugins				\
	-I$(top_srcdir)/shell				\
	-I$(top_srcdir)/backends			\
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	$(TOTEM_PLPARSER_CFLAGS)			\
	$(RHYTHMBOX_CFLAGS)

libaudiocd_la_LIBADD += $(NULL)


gtkbuilderdir = $(plugindatadir)
gtkbuilder_DATA = 					\
	album-info.ui					\
	audiocd-toolbar.ui

noinst_PROGRAMS = test-cd

test_cd_SOURCES = 					\
	test-cd.c					\
	rb-audiocd-info.c				\
	rb-audiocd-info.h				\
	rb-musicbrainz-lookup.c				\
	rb-musicbrainz-lookup.h
test_cd_LDADD = $(RHYTHMBOX_LIBS) $(GSTCDDA_LIBS)

plugin_in_files = audiocd.plugin.in

%.plugin: %.plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

plugin_DATA = $(plugin_in_files:.plugin.in=.plugin)

EXTRA_DIST = $(gtkbuilder_DATA) $(plugin_in_files)

CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)

