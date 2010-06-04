NULL =

noinst_LTLIBRARIES = libsourcesync.la

libsourcesync_la_SOURCES =		\
	rb-sync-settings.c		\
	rb-sync-settings.h		\
	rb-sync-settings-ui.c		\
	rb-sync-settings-ui.h		\
	rb-sync-state.c			\
	rb-sync-state.h			\
	rb-sync-state-ui.c		\
	rb-sync-state-ui.h		\
	$(NULL)

INCLUDES =						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	-I$(top_builddir)/lib 				\
	-I$(top_srcdir)/rhythmdb			\
	-I$(top_srcdir)/podcast				\
	-I$(top_srcdir)/shell				\
	-I$(top_srcdir)/sources				\
	-I$(top_srcdir)/widgets				\
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	$(RHYTHMBOX_CFLAGS)				\
	$(NO_STRICT_ALIASING_CFLAGS)

libsourcesync_la_LDFLAGS = -export-dynamic
