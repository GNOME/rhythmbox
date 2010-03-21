# Art Display Python Plugin

SUBDIRS = artdisplay

plugindir = $(PLUGINDIR)/artdisplay

plugin_in_files = artdisplay.rb-plugin.in
%.rb-plugin: %.rb-plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

plugin_DATA = $(plugin_in_files:.rb-plugin.in=.rb-plugin)

artworkdir = $(plugindir)
artwork_DATA = rhythmbox-missing-artwork.svg lastfm_red_small.png

EXTRA_DIST = $(plugin_in_files) $(artwork_DATA)

CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)
