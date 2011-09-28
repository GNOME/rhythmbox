bULL =

plugindir = $(PLUGINDIR)/visualizer
plugindatadir = $(PLUGINDATADIR)/visualizer
plugin_LTLIBRARIES = libvisualizer.la

libvisualizer_la_SOURCES =			\
	rb-visualizer-menu.c				\
	rb-visualizer-menu.h				\
	rb-visualizer-page.c				\
	rb-visualizer-page.h				\
	rb-visualizer-fullscreen.c			\
	rb-visualizer-fullscreen.h			\
	rb-visualizer-plugin.c				\
	$(NULL)

libvisualizer_la_LDFLAGS = $(PLUGIN_LIBTOOL_FLAGS)
libvisualizer_la_LIBTOOLFLAGS = --tag=disable-static

libvisualizer_la_LIBADD = 			\
	$(top_builddir)/shell/librhythmbox-core.la	\
	-lgstinterfaces-0.10				\
	$(CLUTTER_LIBS)					\
	$(RHYTHMBOX_LIBS)

INCLUDES = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_builddir)				\
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	-DPLUGIN_SRC_DIR=\""$(ROOT_UNINSTALLED_DIR)/plugins/visualizer"\" \
	$(RHYTHMBOX_CFLAGS)				\
	$(CLUTTER_CFLAGS)					\
	-D_BSD_SOURCE

mxthemedir = $(plugindatadir)
mxtheme_DATA = 						\
	visualizer.css					\
	button-active.png				\
	button-disabled.png				\
	button-focus.png				\
	button-hover.png				\
	button.png					\
	visualizer-box.png


#themedir = $(pkgdatadir)/icons/hicolor
#size = 22x22
#context = actions

#icondir = $(themedir)/$(size)/$(context)
#icon_DATA = icons/hicolor/$(size)/$(context)/visualization.png

plugin_in_files = visualizer.plugin.in

%.plugin: %.plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

BUILT_SOURCES =							\
	$(plugin_in_files:.plugin.in=.plugin) 	\
	$(NULL)

plugin_DATA = 			\
	$(BUILT_SOURCES)	\
	$(NULL)

EXTRA_DIST = 			\
	$(icon_DATA)		\
	$(mxtheme_DATA)		\
	$(plugin_in_files)	\
	$(NULL)

CLEANFILES = 			\
	$(BUILT_SOURCES)	\
	$(NULL)

DISTCLEANFILES =		\
	$(BUILT_SOURCES)	\
	$(NULL)
