# Jamendo Python Plugin

SUBDIRS = jamendo

plugindir = $(PLUGINDIR)/jamendo

%.rb-plugin: %.rb-plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache
plugin_DATA = $(plugin_in_files:.rb-plugin.in=.rb-plugin)


plugin_in_files = jamendo.rb-plugin.in

gtkbuilder_DATA =	\
		jamendo-loading.ui			\
		jamendo-prefs.ui			\
		jamendo_logo.png			\
		jamendo_logo_medium.png

themedir = $(pkgdatadir)/icons/hicolor
size = 24x24
context = places
icondir = $(themedir)/$(size)/$(context)
icon_DATA = icons/hicolor/$(size)/$(context)/jamendo.png

gtkbuilderdir = $(plugindir)
EXTRA_DIST = $(plugin_in_files) $(gtkbuilder_DATA) $(icon_DATA)
CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)

