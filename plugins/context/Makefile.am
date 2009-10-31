# Context Panel Python Plugin

SUBDIRS = context

plugindir = $(PLUGINDIR)/context

plugin_in_files = context.rb-plugin.in
%.rb-plugin: %.rb-plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

plugin_DATA = $(plugin_in_files:.rb-plugin.in=.rb-plugin)

tmpldir = $(plugindir)/tmpl
tmpl_DATA = \
	tmpl/album-tmpl.html		\
	tmpl/artist-tmpl.html		\
	tmpl/loading.html		\
	tmpl/lyrics-tmpl.html		\
	tmpl/main.css

imgdir = $(plugindir)/img
img_DATA = \
	img/spinner.gif

EXTRA_DIST = $(plugin_in_files) $(tmpl_DATA) $(img_DATA)

CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)
