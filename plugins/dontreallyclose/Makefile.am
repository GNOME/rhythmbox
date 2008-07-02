# Don't Really Close Python Plugin

plugindir = $(PLUGINDIR)/dontreallyclose

plugin_in_files = dontreallyclose.rb-plugin.in
%.rb-plugin: %.rb-plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

plugin_DATA = $(plugin_in_files:.rb-plugin.in=.rb-plugin)

plugindir = $(PLUGINDIR)/dontreallyclose
plugin_PYTHON = dontreallyclose.py

EXTRA_DIST = $(plugin_in_files) $(plugin_PYTHON)

CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)
