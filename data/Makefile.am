SUBDIRS = ui art glade node-views

@INTLTOOL_SERVER_RULE@
@INTLTOOL_DESKTOP_RULE@

server_in_files = GNOME_Rhythmbox_Shell.server.in
server_DATA = GNOME_Rhythmbox_Shell.server
serverdir = $(libdir)/bonobo/servers

Multimediadir = $(datadir)/applications
Multimedia_in_files = rhythmbox.desktop.in
Multimedia_DATA = $(Multimedia_in_files:.desktop.in=.desktop)

appicon_DATA = rhythmbox.png
appicondir = $(datadir)/pixmaps

EXTRA_DIST = $(server_in_files)     \
             $(server_DATA)         \
	     $(appicon_DATA)        \
	     $(Multimedia_in_files) \
	     $(Multimedia_DATA)
