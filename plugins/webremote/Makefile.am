plugindir = $(PLUGINDIR)/webremote
plugindatadir = $(PLUGINDATADIR)/webremote
plugin_PYTHON = siphash.py webremote.py

plugin_in_files = webremote.plugin.in
%.plugin: %.plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

plugin_DATA = $(plugin_in_files:.plugin.in=.plugin)

gtkbuilderdir = $(plugindatadir)
gtkbuilder_DATA =			\
		webremote-config.ui

EXTRA_DIST = $(plugin_in_files) $(gtkbuilder_DATA)

CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)
