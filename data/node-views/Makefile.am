nodeviewsdir = $(pkgdatadir)/node-views

nodeviews_in_files = rb-node-view-songs.xml.in   \
		     rb-node-view-albums.xml.in  \
		     rb-node-view-artists.xml.in
nodeviews_DATA = $(nodeviews_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(nodeviews_DATA)
