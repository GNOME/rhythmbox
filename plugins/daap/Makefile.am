NULL =

plugindir = $(PLUGINDIR)/daap
plugin_LTLIBRARIES = libdaap.la

libdaap_la_SOURCES = \
	rb-daap-container-record.c		  \
	rb-daap-container-record.h		  \
	rb-daap-plugin.c			  \
	rb-daap-plugin.h			  \
	rb-daap-record.c			  \
	rb-daap-record.h			  \
	rb-daap-record-factory.c		  \
	rb-daap-record-factory.h		  \
	rb-daap-sharing.c			  \
	rb-daap-sharing.h			  \
	rb-daap-source.c			  \
	rb-daap-source.h			  \
	rb-daap-src.c				  \
	rb-daap-src.h				  \
	rb-dmap-container-db-adapter.c		  \
	rb-dmap-container-db-adapter.h		  \
	rb-daap-dialog.c			  \
	rb-daap-dialog.h			  \
	rb-dacp-source.c			  \
	rb-dacp-source.h			  \
	rb-dacp-player.c			  \
	rb-dacp-player.h			  \
	rb-rhythmdb-dmap-db-adapter.c             \
	rb-rhythmdb-dmap-db-adapter.h             \
	rb-rhythmdb-query-model-dmap-db-adapter.c \
	rb-rhythmdb-query-model-dmap-db-adapter.h \
	$(NULL)

libdaap_la_LDFLAGS = $(PLUGIN_LIBTOOL_FLAGS)
libdaap_la_LIBTOOLFLAGS = --tag=disable-static
libdaap_la_LIBADD = 					\
	$(top_builddir)/shell/librhythmbox-core.la	\
	$(DMAPSHARING_LIBS)                             \
	$(DBUS_LIBS)					\
	$(RHYTHMBOX_LIBS)

INCLUDES = 						\
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
	-I$(top_srcdir)/remote				\
	-I$(top_builddir)/remote			\
	-I$(top_builddir)/lib   			\
	-I$(top_srcdir)/plugins				\
	-I$(top_srcdir)/shell				\
	-I$(top_srcdir)/plugins				\
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	$(DMAPSHARING_CFLAGS)				\
	$(RHYTHMBOX_CFLAGS)				\
	$(DBUS_CFLAGS)					\
	-D_XOPEN_SOURCE -D_BSD_SOURCE

if USE_GNOME_KEYRING
libdaap_la_LIBADD += $(GNOME_KEYRING_LIBS)
INCLUDES += $(GNOME_KEYRING_CFLAGS)
endif

gtkbuilderdir = $(plugindir)
gtkbuilder_DATA = daap-prefs.ui

uixmldir = $(plugindir)
uixml_DATA = daap-ui.xml

plugin_in_files = daap.rb-plugin.in

%.rb-plugin: %.rb-plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

BUILT_SOURCES =							\
	$(plugin_in_files:.rb-plugin.in=.rb-plugin) 		\
	$(NULL)

plugin_DATA = 			\
	$(BUILT_SOURCES)	\
	$(top_srcdir)/plugins/daap/remote-icon.png		\
	$(NULL)

EXTRA_DIST = 			\
	$(gtkbuilder_DATA)	\
	$(top_srcdir)/plugins/daap/remote-icon.png		\
	$(uixml_DATA)		\
	$(plugin_in_files)	\
	$(NULL)

CLEANFILES = 			\
	$(BUILT_SOURCES)	\
	$(NULL)

DISTCLEANFILES =		\
	$(BUILT_SOURCES)	\
	$(NULL)

rb-daap-glue.h: rb-daap.xml Makefile
	$(LIBTOOL) --mode=execute $(DBUS_GLIB_BIN)/dbus-binding-tool --prefix=rb_daap --mode=glib-server --output=$@ $<

BUILT_SOURCES += rb-daap-glue.h
EXTRA_DIST += rb-daap.xml

libdaap_la_LIBADD += $(DBUS_LIBS)

