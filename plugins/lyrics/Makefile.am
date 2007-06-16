# Search Lyrics Python Plugin

SUBDIRS = lyrics

plugindir = $(PLUGINDIR)/lyrics

%.rb-plugin: %.rb-plugin.desktop.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache
plugin_DATA = $(plugin_in_files:.rb-plugin.desktop.in=.rb-plugin)


plugin_in_files = lyrics.rb-plugin.desktop.in

glade_DATA =	\
		lyrics-prefs.glade

gladedir = $(plugindir)
EXTRA_DIST = $(plugin_in_files) $(glade_DATA)
CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)

