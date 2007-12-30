# Coherence UPNP Python Plugin

SUBDIRS = upnp_coherence

plugindir = $(PLUGINDIR)/upnp_coherence

%.rb-plugin: %.rb-plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache
plugin_DATA = $(plugin_in_files:.rb-plugin.in=.rb-plugin)


plugin_in_files = coherence.rb-plugin.in

glade_DATA = 

gladedir = $(plugindir)
EXTRA_DIST = $(plugin_in_files) $(glade_DATA)
CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)

