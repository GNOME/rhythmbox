## arch-tag: Automake rules for XML user interface definition stuff

uixmldir = $(datadir)/gnome-2.0/ui
uixml_in_files = rhythmbox-ui.xml.in
uixml_DATA = $(uixml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(uixml_in_files)
