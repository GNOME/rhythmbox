# Magnatune Store Python Plugin

plugindir = $(PLUGINDIR)/magnatune
plugindatadir = $(PLUGINDATADIR)/magnatune
plugin_PYTHON =                       \
       MagnatuneSource.py             \
       DownloadAlbumHandler.py        \
       TrackListHandler.py            \
       MagnatuneAccount.py            \
       magnatune.py

%.plugin: %.plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache
plugin_DATA = $(plugin_in_files:.plugin.in=.plugin)

plugin_in_files = magnatune.plugin.in

gtkbuilderdir = $(plugindatadir)
gtkbuilder_DATA =	\
		magnatune-loading.ui			\
		magnatune-popup.ui			\
		magnatune-prefs.ui			\
		magnatune-toolbar.ui			\
		magnatune_logo_color_small.png		\
		magnatune_logo_color_tiny.png

icondir = $(plugindatadir)/icons/hicolor/scalable/places
icon_DATA = icons/hicolor/scalable/places/magnatune-symbolic.svg

EXTRA_DIST = $(plugin_in_files) $(gtkbuilder_DATA) $(icon_DATA)
CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)
