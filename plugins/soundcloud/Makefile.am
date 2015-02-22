plugindir = $(PLUGINDIR)/soundcloud
plugindatadir = $(PLUGINDATADIR)/soundcloud
plugin_PYTHON = soundcloud.py

plugin_in_files = soundcloud.plugin.in
%.plugin: %.plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

plugin_DATA = $(plugin_in_files:.plugin.in=.plugin)

gtkbuilderdir = $(plugindatadir)
gtkbuilder_DATA = soundcloud.ui powered-by-soundcloud.png

icondir = $(plugindatadir)/icons/hicolor/scalable/places
icon_DATA = icons/hicolor/scalable/places/soundcloud-symbolic.svg

EXTRA_DIST = $(plugin_in_files) $(gtkbuilder_DATA) $(icon_DATA)

CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)
