bin_PROGRAMS = rhythmbox-client
rhythmbox_client_SOURCES = rb-client.c
rhythmbox_client_LDADD =				\
	$(top_builddir)/lib/librb.la			\
	$(RB_CLIENT_LIBS)				\
	$(DBUS_LIBS)

INCLUDES = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib                        	\
	-I$(top_srcdir)/shell                        	\
	-I$(top_builddir)/shell                        	\
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	$(RHYTHMBOX_CFLAGS)				\
	$(DBUS_CFLAGS)					\
	-D__EXTENSIONS__ -D_BSD_SOURCE

EXTRA_DIST = rb-client.c rb-print-playing.py rb-set-rating.py

