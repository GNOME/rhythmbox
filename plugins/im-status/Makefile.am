# IM Status Python Plugin

plugin_in_files = im-status.plugin.in
%.plugin: %.plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

plugin_DATA = $(plugin_in_files:.plugin.in=.plugin)

plugindir = $(PLUGINDIR)/im-status
plugin_PYTHON = im-status.py

EXTRA_DIST = $(plugin_in_files) $(plugin_PYTHON)

CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)
