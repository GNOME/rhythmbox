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

schemadir = $(sysconfdir)/gconf/schemas
schema_DATA = rhythmbox.schemas

install-schemas: $(schema_DATA)
	GCONF_CONFIG_SOURCE=$(GCONF_SCHEMAS_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $(top_srcdir)/data/$(schema_DATA)

if INSTALL_SCHEMAS
install-data-local: install-schemas
endif

CLEANFILES = GNOME_Rhythmbox_Shell.server rhythmbox.desktop

EXTRA_DIST = $(server_in_files)     \
	     $(appicon_DATA)        \
	     $(Multimedia_in_files) \
	     $(schema_DATA)
