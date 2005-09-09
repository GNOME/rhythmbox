noinst_LTLIBRARIES = libdaapsharing.la

libdaapsharing_la_SOURCES = 

if USE_DAAP
libdaapsharing_la_SOURCES += \
		rb-daap-sharing.c	\
		rb-daap-sharing.h 	\
		rb-daap-share.c		\
		rb-daap-share.h 	\
		rb-daap-structure.c 	\
		rb-daap-structure.h 	\
		rb-daap-mdns.c 		\
		rb-daap-mdns.h 		\
		rb-daap-connection.c 	\
		rb-daap-connection.h 	\
		rb-daap-src.c 		\
		rb-daap-src.h		\
		rb-daap-dialog.c	\
		rb-daap-dialog.h
endif

INCLUDES =						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	-I$(top_builddir)/lib 				\
	-I$(top_srcdir)/corba 				\
	-I$(top_builddir)/corba				\
	-I$(top_srcdir)/rhythmdb			\
	-I$(top_srcdir)/library 			\
	-I$(top_srcdir)/iradio				\
	-I$(top_srcdir)/widgets				\
	-I$(top_srcdir)/shell				\
	-I$(top_srcdir)/sources				\
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	$(WARN_CFLAGS)					\
	$(RHYTHMBOX_CFLAGS)				\
	$(SOUP_CFLAGS)					\
	$(MDNS_CFLAGS)

libdaapsharing_la_LDFLAGS = -export-dynamic

