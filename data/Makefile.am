## arch-tag: Automake rules for various data files

SUBDIRS = ui art glade node-views

@INTLTOOL_SERVER_RULE@
@INTLTOOL_DESKTOP_RULE@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = rhythmbox.pc

server_in_files = GNOME_Rhythmbox.server.in
server_DATA = GNOME_Rhythmbox.server
serverdir = $(libdir)/bonobo/servers

Multimediadir = $(datadir)/applications
Multimedia_in_files = rhythmbox.desktop.in
Multimedia_DATA = $(Multimedia_in_files:.desktop.in=.desktop)

appicon_DATA = rhythmbox.png
appicondir = $(datadir)/pixmaps

schemadir = $(sysconfdir)/gconf/schemas
schema_DATA = rhythmbox.schemas

keys_DATA = rhythmbox.keys
keysdir = $(datadir)/mime-info/

applicationsdir = $(datadir)/application-registry
applications_DATA = rhythmbox.applications

iradio-initial.xml:

iradio_DATA = iradio-initial.pls
iradiodir = $(datadir)/rhythmbox

rhythmbox.keys: generate-keys.sh rhythmbox.applications
	$(srcdir)/generate-keys.sh $(srcdir)/rhythmbox.applications \
	> rhythmbox.keys

if GCONF_SCHEMAS_INSTALL
install-data-local: $(schema_DATA)
	GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $(srcdir)/$(schema_DATA);
endif

EXTRA_DIST = $(server_in_files)     \
	     $(appicon_DATA)        \
	     $(Multimedia_in_files) \
	     $(applications_DATA)   \
	     $(schema_DATA)	    \
	     $(iradio_DATA)	    \
		generate-keys.sh

CLEANFILES = rhythmbox.desktop rhythmbox.keys GNOME_Rhythmbox.server
