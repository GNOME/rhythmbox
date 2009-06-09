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
lib_LTLIBRARIES = librhythmbox-core.la

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
	-DRB_PLUGIN_DIR=\"$(PLUGINDIR)\"		\
	$(RHYTHMBOX_CFLAGS)				\
	$(TOTEM_PLPARSER_CFLAGS)			\
	$(DBUS_CFLAGS)					\
	-D_XOPEN_SOURCE -D__EXTENSIONS__ -D_BSD_SOURCE

librhythmbox_core_la_SOURCES =				\
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
	rb-missing-plugins.h				\
	\
	rb-plugin.h					\
	rb-plugin.c					\
	rb-module.h					\
	rb-module.c					\
	rb-plugin-manager.h				\
	rb-plugin-manager.c				\
	rb-plugins-engine.h				\
	rb-plugins-engine.c

librhythmbox_core_la_LIBADD =				\
	$(top_builddir)/sources/libsourcesimpl.la	\
	$(top_builddir)/podcast/librbpodcast.la	        \
	$(top_builddir)/sources/libsources.la	        \
	$(top_builddir)/metadata/librbmetadata.la	\
	$(top_builddir)/widgets/librbwidgets.la         \
	$(top_builddir)/rhythmdb/librhythmdb.la		\
	$(top_builddir)/backends/librbbackends.la	\
	$(top_builddir)/lib/librb.la			\
	$(DBUS_LIBS)					\
	$(TOTEM_PLPARSER_LIBS)				\
	$(RHYTHMBOX_LIBS)				\
	-lgstpbutils-0.10				\
	-lgstcontroller-0.10

librhythmbox_core_la_LDFLAGS = -export-dynamic -no-undefined
librhythmbox_core_la_LIBTOOLFLAGS = --tag=disable-static

if ENABLE_PYTHON

librhythmbox_core_la_SOURCES +=				\
	rb-python-module.h				\
	rb-python-module.c				\
	rb-python-plugin.h				\
	rb-python-plugin.c

INCLUDES += $(PYGTK_CFLAGS)  $(PYTHON_CFLAGS)

librhythmbox_core_la_LIBADD += $(PYTHON_LIBS) $(top_builddir)/bindings/python/rb.la
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

rhythmbox_LDADD = 					\
	librhythmbox-core.la				\
	$(RHYTHMBOX_LIBS)				\
	$(DBUS_LIBS)

rhythmbox_LDFLAGS = -export-dynamic -no-undefined


BUILT_SOURCES += $(tab_files)

CLEANFILES += $(BUILT_SOURCES)
