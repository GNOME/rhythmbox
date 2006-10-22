# Art Display Python Plugin

SUBDIRS = artdisplay

plugindir = $(PLUGINDIR)/artdisplay

plugin_in_files = artdisplay.rb-plugin.desktop.in
%.rb-plugin: %.rb-plugin.desktop.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

plugin_DATA = $(plugin_in_files:.rb-plugin.desktop.in=.rb-plugin) \
	rhythmbox-missing-artwork.svg

EXTRA_DIST = $(plugin_in_files)

CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)
