## arch-tag: Automake rules for Rhythmbox DBus interface

bin_PROGRAMS = rhythmbox-client
rhythmbox_client_SOURCES = rb-client.c
rhythmbox_client_LDADD =				\
	$(top_builddir)/lib/librb.la			\
	$(DBUS_LIBS)					\
	$(RB_CLIENT_LIBS)

INCLUDES = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib                        	\
	-I$(top_srcdir)/shell                        	\
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	$(WARN_CFLAGS)					\
	$(RB_CLIENT_CFLAGS)				\
	$(DBUS_CFLAGS)					\
	-D_XOPEN_SOURCE -D__EXTENSIONS__ -D_BSD_SOURCE

EXTRA_DIST = rb-print-playing.py rb-set-rating.py

