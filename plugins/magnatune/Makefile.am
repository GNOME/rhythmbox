# Magnatune Store Python Plugin

# this plugin is disabled until we can access gnome-keyring

plugindir = $(PLUGINDIR)/magnatune
plugindatadir = $(PLUGINDATADIR)/magnatune
noinst_PYTHON =                       \
       MagnatuneSource.py             \
       BuyAlbumHandler.py             \
       TrackListHandler.py            \
       magnatune.py

# %.plugin: %.plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache
# plugin_DATA = $(plugin_in_files:.plugin.in=.plugin)

plugin_in_files = magnatune.plugin.in

gtkbuilderdir = $(plugindatadir)
noinst_DATA =	\
		magnatune-loading.ui			\
		magnatune-prefs.ui			\
		magnatune_logo_color_small.png		\
		magnatune_logo_color_tiny.png

themedir = $(pkgdatadir)/icons/hicolor
size = 24x24
context = places
icondir = $(themedir)/$(size)/$(context)
noinst_DATA = icons/hicolor/$(size)/$(context)/magnatune.png

EXTRA_DIST = $(plugin_in_files) $(gtkbuilder_DATA) $(icon_DATA)
CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)
