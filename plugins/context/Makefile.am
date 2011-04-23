# Context Pane Python Plugin

plugindir = $(PLUGINDIR)/context
plugindatadir = $(PLUGINDATADIR)/context
plugin_PYTHON = 			\
	AlbumTab.py			\
	ArtistTab.py			\
	ContextView.py			\
	LastFM.py			\
	LinksTab.py			\
	LyricsTab.py			\
	context.py

plugin_in_files = context.plugin.in
%.plugin: %.plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

plugin_DATA = $(plugin_in_files:.plugin.in=.plugin)

tmpldir = $(plugindatadir)/tmpl
tmpl_DATA = \
	tmpl/album-tmpl.html		\
	tmpl/artist-tmpl.html		\
	tmpl/loading.html		\
	tmpl/links-tmpl.html		\
	tmpl/lyrics-tmpl.html		\
	tmpl/main.css

imgdir = $(plugindatadir)/img
img_DATA = \
	img/lastfm.png			\
	img/spinner.gif			\
	img/links/Allmusic16x16.png	\
	img/links/Discogs16x16.png	\
	img/links/Wikipedia16x16.png

EXTRA_DIST = $(plugin_in_files) $(tmpl_DATA) $(img_DATA)

CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)
