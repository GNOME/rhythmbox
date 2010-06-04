plugindir = $(PLUGINDIR)/generic-player
plugin_LTLIBRARIES = libgeneric-player.la

libgeneric_player_la_SOURCES =				\
	rb-generic-player-plugin.c			\
	rb-generic-player-source.c 			\
	rb-generic-player-source.h	 		\
	rb-generic-player-playlist-source.c		\
	rb-generic-player-playlist-source.h		\
	rb-nokia770-source.c				\
	rb-nokia770-source.h				\
	rb-psp-source.c					\
	rb-psp-source.h

libgeneric_player_la_LIBTOOLFLAGS = --tag=disable-static
libgeneric_player_la_LDFLAGS = 				\
	$(top_builddir)/shell/librhythmbox-core.la	\
	$(TOTEM_PLPARSER_LIBS)				\
	$(PLUGIN_LIBTOOL_FLAGS)

INCLUDES = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib                        	\
	-I$(top_srcdir)/lib/libmediaplayerid            \
	-I$(top_srcdir)/metadata                       	\
	-I$(top_srcdir)/player                       	\
	-I$(top_srcdir)/rhythmdb                       	\
	-I$(top_srcdir)/widgets                    	\
	-I$(top_srcdir)/sources                    	\
	-I$(top_srcdir)/sources/sync                   	\
	-I$(top_srcdir)/iradio                    	\
	-I$(top_srcdir)/podcast                    	\
	-I$(top_srcdir)/remote				\
	-I$(top_builddir)/remote			\
	-I$(top_srcdir)/plugins				\
	-I$(top_srcdir)/shell				\
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	$(TOTEM_PLPARSER_CFLAGS)			\
	$(RHYTHMBOX_CFLAGS)				\
	-D_XOPEN_SOURCE -D_BSD_SOURCE

uixmldir = $(plugindir)
uixml_DATA = generic-player-ui.xml

gtkbuilderdir = $(plugindir)
gtkbuilder_DATA = generic-player-info.ui

plugin_in_files = generic-player.rb-plugin.in

%.rb-plugin: %.rb-plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

plugin_DATA = $(plugin_in_files:.rb-plugin.in=.rb-plugin)

EXTRA_DIST = $(uixml_DATA) $(gtkbuilder_DATA) $(plugin_in_files)

CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)

