plugindir = $(PLUGINDIR)/replaygain
plugindatadir = $(PLUGINDATADIR)/replaygain
plugin_PYTHON =				\
	config.py			\
	player.py			\
	replaygain.py

%.plugin: %.plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

plugin_in_files = replaygain.plugin.in

gtkbuilderdir = $(plugindatadir)
gtkbuilder_DATA =			\
		replaygain-prefs.ui

EXTRA_DIST = $(plugin_in_files) $(gtkbuilder_DATA)
CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)

BUILT_SOURCES =	$(plugin_in_files:.plugin.in=.plugin)

plugin_DATA = $(BUILT_SOURCES)
