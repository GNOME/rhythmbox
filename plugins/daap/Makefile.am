NULL =

plugindir = $(PLUGINDIR)/daap
plugin_LTLIBRARIES = libdaap.la

libdaap_la_SOURCES = \
	rb-daap-plugin.c			\
	rb-daap-plugin.h			\
	rb-daap-source.c			\
	rb-daap-source.h			\
	rb-daap-sharing.c			\
	rb-daap-sharing.h			\
	rb-daap-share.c				\
	rb-daap-share.h				\
	rb-daap-structure.c			\
	rb-daap-structure.h 			\
	rb-daap-mdns-publisher.h		\
	rb-daap-mdns-browser.h			\
	rb-daap-connection.c			\
	rb-daap-connection.h			\
	rb-daap-hash.c				\
	rb-daap-hash.h				\
	rb-daap-src.c				\
	rb-daap-src.h				\
	rb-daap-dialog.c			\
	rb-daap-dialog.h			\
	$(NULL)

if USE_HOWL
libdaap_la_SOURCES += \
	rb-daap-mdns-publisher-howl.c		\
	rb-daap-mdns-browser-howl.c		\
	$(NULL)
else
libdaap_la_SOURCES += \
	rb-daap-mdns-publisher-avahi.c		\
	rb-daap-mdns-browser-avahi.c		\
	$(NULL)
endif


libdaap_la_LDFLAGS = $(PLUGIN_LIBTOOL_FLAGS)
libdaap_la_LIBADD = 					\
	$(SOUP_LIBS)					\
	$(MDNS_LIBS)

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
	$(SOUP_CFLAGS)					\
	$(MDNS_CFLAGS)					\
	$(RHYTHMBOX_CFLAGS)				\
	$(WARN_CFLAGS)					\
	-D_XOPEN_SOURCE -D_BSD_SOURCE

if USE_GNOME_KEYRING
libdaap_la_LIBADD += $(GNOME_KEYRING_LIBS)
INCLUDES += $(GNOME_KEYRING_CFLAGS)
endif

gladedir = $(plugindir)
glade_DATA = daap-prefs.glade

uixmldir = $(plugindir)
uixml_DATA = daap-ui.xml

plugin_in_files = daap.rb-plugin.desktop.in

%.rb-plugin: %.rb-plugin.desktop.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

BUILT_SOURCES =							\
	$(plugin_in_files:.rb-plugin.desktop.in=.rb-plugin) 	\
	$(NULL)

plugin_DATA = 			\
	$(BUILT_SOURCES)	\
	$(NULL)

EXTRA_DIST = 			\
	$(glade_DATA)		\
	$(uixml_DATA)		\
	$(plugin_in_files)	\
	$(NULL)

CLEANFILES = 			\
	$(BUILT_SOURCES)	\
	$(NULL)

DISTCLEANFILES =		\
	$(BUILT_SOURCES)	\
	$(NULL)

