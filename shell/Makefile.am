## arch-tag: Automake rules for main Rhythmbox shell

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
noinst_LTLIBRARIES = librbshell.la

INCLUDES = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib                        	\
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
	$(RHYTHMBOX_CFLAGS)				\
	$(TOTEM_PLPARSER_CFLAGS)			\
	$(DBUS_CFLAGS)					\
	-D_XOPEN_SOURCE -D__EXTENSIONS__ -D_BSD_SOURCE

librbshell_la_SOURCES =              			\
	rb-shell.c					\
	rb-shell.h					\
	rb-shell-player.c				\
	rb-shell-player.h				\
	rb-source-header.c				\
	rb-source-header.h				\
	rb-statusbar.c					\
	rb-statusbar.h					\
	rb-shell-preferences.c				\
	rb-shell-preferences.h				\
	rb-shell-clipboard.c				\
	rb-shell-clipboard.h				\
	rb-playlist-manager.c				\
	rb-playlist-manager.h				\
	rb-removable-media-manager.c			\
	rb-removable-media-manager.h			\
	rb-history.c					\
	rb-history.h					\
	rb-play-order.c					\
	rb-play-order.h					\
	rb-play-order-linear.c				\
	rb-play-order-linear.h				\
	rb-play-order-linear-loop.c			\
	rb-play-order-linear-loop.h			\
	rb-play-order-queue.c				\
	rb-play-order-queue.h				\
	rb-play-order-shuffle.c				\
	rb-play-order-shuffle.h				\
	rb-play-order-random.c				\
	rb-play-order-random.h				\
	rb-play-order-random-equal-weights.c		\
	rb-play-order-random-equal-weights.h		\
	rb-play-order-random-by-age.c			\
	rb-play-order-random-by-age.h			\
	rb-play-order-random-by-age-and-rating.c	\
	rb-play-order-random-by-age-and-rating.h	\
	rb-play-order-random-by-rating.c		\
	rb-play-order-random-by-rating.h		\
	rb-missing-plugins.c				\
	rb-missing-plugins.h

rhythmbox_LDADD =					\
	librbshell.la					\
	$(top_builddir)/corelib/librhythmbox-core.la	\
	$(top_builddir)/sources/libsourcesimpl.la	\
	$(top_builddir)/podcast/librbpodcast.la	        \
	$(top_builddir)/plugins/librbplugins.la		\
	-lgstpbutils-0.10

if ENABLE_PYTHON
INCLUDES += $(PYGTK_CFLAGS)  $(PYTHON_CFLAGS)
rhythmbox_LDADD += $(PYTHON_LIBS) $(top_builddir)/bindings/python/rb.la
endif

rb-shell-glue.h: rb-shell.xml Makefile
	$(LIBTOOL) --mode=execute $(DBUS_GLIB_BIN)/dbus-binding-tool --prefix=rb_shell --mode=glib-server --output=$@ $<
rb-shell-player-glue.h: rb-shell-player.xml Makefile
	$(LIBTOOL) --mode=execute $(DBUS_GLIB_BIN)/dbus-binding-tool --prefix=rb_shell_player --mode=glib-server --output=$@ $<
rb-playlist-manager-glue.h: rb-playlist-manager.xml Makefile
	$(LIBTOOL) --mode=execute $(DBUS_GLIB_BIN)/dbus-binding-tool --prefix=rb_playlist_manager --mode=glib-server --output=$@ $<
rb-shell-binding.h: rb-shell.xml Makefile
	$(LIBTOOL) --mode=execute $(DBUS_GLIB_BIN)/dbus-binding-tool --prefix=rb_shell --mode=glib-client --output=$@ $<
rb-shell-player-binding.h: rb-shell-player.xml Makefile
	$(LIBTOOL) --mode=execute $(DBUS_GLIB_BIN)/dbus-binding-tool --prefix=rb_shell --mode=glib-client --output=$@ $<

BUILT_SOURCES += rb-shell-glue.h rb-shell-binding.h rb-shell-player-glue.h rb-shell-player-binding.h rb-playlist-manager-glue.h
EXTRA_DIST += rb-shell.xml rb-shell-player.xml rb-playlist-manager.xml
rhythmbox_LDADD += $(DBUS_LIBS)

rhythmbox_LDADD += \
	$(top_builddir)/backends/librbbackends.la	\
	$(TOTEM_PLPARSER_LIBS)				\
	$(HAL_LIBS)					\
	$(RHYTHMBOX_LIBS)				\
	-lgstcontroller-0.10

rhythmbox_LDFLAGS = -export-dynamic -no-undefined


BUILT_SOURCES += $(tab_files)

CLEANFILES += $(BUILT_SOURCES)
