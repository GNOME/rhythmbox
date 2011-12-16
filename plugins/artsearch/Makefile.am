# art search plugin

plugindir = $(PLUGINDIR)/artsearch
plugindatadir = $(PLUGINDATADIR)/artsearch
plugin_PYTHON = 			\
	artsearch.py			\
	lastfm.py			\
	local.py			\
	musicbrainz.py			\
	oldcache.py

plugin_in_files = artsearch.plugin.in
%.plugin: %.plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

plugin_DATA = $(plugin_in_files:.plugin.in=.plugin)

EXTRA_DIST = $(plugin_in_files) $(artwork_DATA)

CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)
