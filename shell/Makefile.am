BUILT_SOURCES = 
CLEANFILES =
EXTRA_DIST =

AUTHORS.tab : $(top_srcdir)/AUTHORS
	sed -e 's/^/"/' -e 's/$$/",/' < $(top_srcdir)/AUTHORS > $@.tmp
	mv $@.tmp $@

MAINTAINERS.tab : $(top_srcdir)/MAINTAINERS
	sed -e 's/^/"/' -e 's/$$/",/' < $(top_srcdir)/MAINTAINERS > $@.tmp
	mv $@.tmp $@

MAINTAINERS.old.tab : $(top_srcdir)/MAINTAINERS.old
	sed -e 's/^/"/' -e 's/$$/",/' < $(top_srcdir)/MAINTAINERS.old > $@.tmp
	mv $@.tmp $@

DOCUMENTERS.tab : $(top_srcdir)/DOCUMENTERS
	sed -e 's/^/"/' -e 's/$$/",/' < $(top_srcdir)/DOCUMENTERS > $@.tmp
	mv $@.tmp $@

tab_files = AUTHORS.tab MAINTAINERS.tab MAINTAINERS.old.tab DOCUMENTERS.tab

bin_PROGRAMS = rhythmbox
rhythmbox_SOURCES = main.c
lib_LTLIBRARIES = librhythmbox-core.la

INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib                        	\
	-I$(top_srcdir)/lib/libmediaplayerid		\
	-I$(top_srcdir)/metadata                       	\
	-I$(top_srcdir)/rhythmdb                       	\
	-I$(top_srcdir)/widgets                    	\
	-I$(top_srcdir)/sources                    	\
	-I$(top_srcdir)/podcast                    	\
	-I$(top_builddir)/lib				\
	-I$(top_srcdir)/plugins				\
	-I$(top_srcdir)/backends			\
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	-DLIBDIR=\""$(libdir)"\"			\
	$(RHYTHMBOX_CFLAGS)				\
	$(TOTEM_PLPARSER_CFLAGS)			\
	$(DBUS_CFLAGS)					\
	$(GUDEV_CFLAGS)					\
	$(WEBKIT_FLAGS)				\
	-D__EXTENSIONS__ -D_BSD_SOURCE


shellincludedir = $(includedir)/rhythmbox/shell
shellinclude_HEADERS =					\
	rb-shell.h					\
	rb-shell-player.h				\
	rb-shell-preferences.h				\
	rb-playlist-manager.h				\
	rb-removable-media-manager.h			\
	rb-history.h					\
	rb-play-order.h					\
	rb-track-transfer-batch.h			\
	rb-track-transfer-queue.h

librhythmbox_core_la_SOURCES =				\
	$(shellinclude_HEADERS)				\
	rb-history.c					\
	rb-play-order.c					\
	rb-play-order-linear.c				\
	rb-play-order-linear.h				\
	rb-play-order-linear-loop.c			\
	rb-play-order-linear-loop.h			\
	rb-play-order-queue.c				\
	rb-play-order-queue.h				\
	rb-play-order-random.c				\
	rb-play-order-random.h				\
	rb-play-order-random-by-age.c			\
	rb-play-order-random-by-age.h			\
	rb-play-order-random-by-age-and-rating.c	\
	rb-play-order-random-by-age-and-rating.h	\
	rb-play-order-random-by-rating.c		\
	rb-play-order-random-by-rating.h		\
	rb-play-order-random-equal-weights.c		\
	rb-play-order-random-equal-weights.h		\
	rb-play-order-shuffle.c				\
	rb-play-order-shuffle.h				\
	rb-playlist-manager.c				\
	rb-removable-media-manager.c			\
	rb-shell.c					\
	rb-shell-clipboard.c				\
	rb-shell-clipboard.h				\
	rb-shell-player.c				\
	rb-shell-preferences.c				\
	rb-shell-preferences.h				\
	rb-statusbar.c					\
	rb-statusbar.h					\
	rb-track-transfer-batch.c			\
	rb-track-transfer-queue.c

librhythmbox_core_la_LIBADD =				\
	$(top_builddir)/sources/libsources.la	        \
	$(top_builddir)/sources/sync/libsourcesync.la	\
	$(top_builddir)/podcast/librbpodcast.la	        \
	$(top_builddir)/metadata/librbmetadata.la	\
	$(top_builddir)/widgets/librbwidgets.la         \
	$(top_builddir)/rhythmdb/librhythmdb.la		\
	$(top_builddir)/backends/librbbackends.la	\
	$(top_builddir)/lib/librb.la			\
	$(top_builddir)/lib/libmediaplayerid/libmediaplayerid.la		\
	$(DBUS_LIBS)					\
	$(TOTEM_PLPARSER_LIBS)				\
	$(GUDEV_LIBS)					\
	$(WEBKIT_LIBS)				\
	$(RHYTHMBOX_LIBS)				\
	-lgstpbutils-0.10				\
	-lgstcontroller-0.10				\
	-lgsttag-0.10

librhythmbox_core_la_LDFLAGS = 				\
	-version-info $(RHYTHMBOX_CORE_CURRENT):$(RHYTHMBOX_CORE_REVISION):$(RHYTHMBOX_CORE_AGE)	\
	-export-dynamic -no-undefined
librhythmbox_core_la_LIBTOOLFLAGS = --tag=disable-static


rhythmbox_LDADD = 					\
	librhythmbox-core.la				\
	$(TOTEM_PLPARSER_LIBS)				\
	$(RHYTHMBOX_LIBS)				\
	$(DBUS_LIBS)					\
	$(INTROSPECTION_LIBS)

rhythmbox_LDFLAGS = -export-dynamic -no-undefined

if ENABLE_PYTHON
rhythmbox_LDADD += $(PYTHON_LIBS)
endif

BUILT_SOURCES += $(tab_files)

CLEANFILES += $(BUILT_SOURCES)
