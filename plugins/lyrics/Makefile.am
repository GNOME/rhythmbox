# Search Lyrics Python Plugin

plugindir = $(PLUGINDIR)/lyrics
plugindatadir = $(PLUGINDATADIR)/lyrics
plugin_PYTHON =				\
       LyricsParse.py			\
       LyricsSites.py			\
       LyricsConfigureDialog.py		\
       lyrics.py			\
       AstrawebParser.py		\
       LyricWikiParser.py		\
       WinampcnParser.py		\
       TerraParser.py			\
       DarkLyricsParser.py

%.plugin: %.plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache
plugin_DATA = $(plugin_in_files:.plugin.in=.plugin)

plugin_in_files = lyrics.plugin.in

gtkbuilderdir = $(plugindatadir)
gtkbuilder_DATA =	\
		lyrics-prefs.ui

EXTRA_DIST = $(plugin_in_files) $(gtkbuilder_DATA)
CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)
