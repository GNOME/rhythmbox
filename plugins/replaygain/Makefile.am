SUBDIRS = replaygain

plugindir = $(PLUGINDIR)/replaygain

%.rb-plugin: %.rb-plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache
plugin_DATA = $(plugin_in_files:.rb-plugin.in=.rb-plugin)


plugin_in_files = replaygain.rb-plugin.in

gtkbuilder_DATA =			\
		replaygain-prefs.ui

gtkbuilderdir = $(plugindir)
EXTRA_DIST = $(plugin_in_files) $(gtkbuilder_DATA)
CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)
