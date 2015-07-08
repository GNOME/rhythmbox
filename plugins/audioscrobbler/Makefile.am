NULL =

plugindir = $(PLUGINDIR)/audioscrobbler
plugindatadir = $(PLUGINDATADIR)/audioscrobbler
icondir = $(plugindatadir)/icons/hicolor/scalable/places

plugin_LTLIBRARIES = libaudioscrobbler.la
noinst_LTLIBRARIES = libaudioscrobblertest.la

libaudioscrobbler_la_SOURCES = \
	rb-audioscrobbler-plugin.c			\
	rb-audioscrobbler-entry.h			\
	rb-audioscrobbler-entry.c			\
	rb-audioscrobbler-profile-page.h		\
	rb-audioscrobbler-profile-page.c		\
	rb-audioscrobbler-account.h			\
	rb-audioscrobbler-account.c			\
	rb-audioscrobbler-service.h			\
	rb-audioscrobbler-service.c			\
	rb-audioscrobbler-user.h			\
	rb-audioscrobbler-user.c			\
	rb-audioscrobbler.c				\
	rb-audioscrobbler.h				\
	rb-audioscrobbler-radio-source.c		\
	rb-audioscrobbler-radio-source.h		\
	rb-audioscrobbler-radio-track-entry-type.h	\
	rb-audioscrobbler-radio-track-entry-type.c	\
	rb-audioscrobbler-play-order.c			\
	rb-audioscrobbler-play-order.h

libaudioscrobblertest_la_SOURCES = \
	rb-audioscrobbler-entry.c			\
	rb-audioscrobbler-radio-track-entry-type.c

libaudioscrobbler_la_LDFLAGS = $(PLUGIN_LIBTOOL_FLAGS)
libaudioscrobbler_la_LIBTOOLFLAGS = --tag=disable-static
libaudioscrobbler_la_LIBADD = 				\
	$(top_builddir)/shell/librhythmbox-core.la	\
	$(TOTEM_PLPARSER_LIBS)				\
	$(JSON_GLIB_LIBS)				\
	$(RHYTHMBOX_LIBS)

AM_CPPFLAGS = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib                        	\
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
	$(TOTEM_PLPARSER_CFLAGS)			\
	$(JSON_GLIB_CFLAGS)				\
	$(RHYTHMBOX_CFLAGS)

gtkbuilderdir = $(plugindatadir)
gtkbuilder_DATA =					\
	audioscrobbler-preferences.ui			\
	audioscrobbler-profile.ui

plugin_in_files = audioscrobbler.plugin.in

%.plugin: %.plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

BUILT_SOURCES =						\
	$(plugin_in_files:.plugin.in=.plugin) 		\
	$(NULL)

plugin_DATA =							\
	$(BUILT_SOURCES)					\
	$(NULL)

icon_DATA = icons/hicolor/scalable/places/Last.fm-symbolic.svg

EXTRA_DIST = 					\
	$(plugin_in_files)			\
	$(gtkbuilder_DATA)			\
	$(icon_DATA)				\
	$(NULL)

CLEANFILES = 			\
	$(BUILT_SOURCES)	\
	$(NULL)

DISTCLEANFILES =		\
	$(BUILT_SOURCES)	\
	$(NULL)
