# ListenBrainz plugin
plugindir = $(PLUGINDIR)/listenbrainz
plugindatadir = $(PLUGINDATADIR)/listenbrainz

plugin_PYTHON = 			\
	client.py			\
	listenbrainz.py			\
	queue.py			\
	settings.py

plugin_in_files = listenbrainz.plugin.in
%.plugin: %.plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

plugin_DATA = $(plugin_in_files:.plugin.in=.plugin)

gtkbuilderdir = $(plugindatadir)
gtkbuilder_DATA =			\
		settings.ui

EXTRA_DIST = $(plugin_in_files) $(gtkbuilder_DATA)

CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)
