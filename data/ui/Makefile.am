uixmldir = $(datadir)/gnome-2.0/ui
uixml_in_files = rhythmbox-ui.xml.in           \
                 rhythmbox-library-view.xml.in \
		 rhythmbox-group-view.xml.in   \
                 rhythmbox-test-view-2.xml.in
uixml_DATA = $(uixml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(uixml_in_files)

CLEANFILES = $(uixml_in_files)
