## arch-tag: Automake rules for various data files

SUBDIRS = ui glade art

@INTLTOOL_XML_RULE@
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

schemadir = @GCONF_SCHEMA_FILE_DIR@
schema_DATA = rhythmbox.schemas

keys_DATA = rhythmbox.keys
keysdir = $(datadir)/mime-info/

applicationsdir = $(datadir)/application-registry
applications_DATA = rhythmbox.applications

iradio-initial.xml:

iradio_DATA = iradio-initial.pls
iradiodir = $(datadir)/rhythmbox

playlists_in_files = playlists.xml.in
playlists_DATA = playlists.xml
playlistsdir = $(datadir)/rhythmbox

rhythmbox.keys: generate-keys.sh rhythmbox.applications
	$(srcdir)/generate-keys.sh $(srcdir)/rhythmbox.applications \
	> rhythmbox.keys

# Dbus service file
servicedir = $(datadir)/dbus-1/services
service_in_files = org.gnome.Rhythmbox.service.in
service_DATA = $(service_in_files:.service.in=.service)

# Rule to make the service file with bindir expanded
$(service_DATA): $(service_in_files) Makefile
	@sed -e "s|\@bindir\@|$(bindir)|" $< > $@

if GCONF_SCHEMAS_INSTALL
install-data-local: $(schema_DATA)
	GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $(srcdir)/$(schema_DATA);
endif

EXTRA_DIST = $(server_in_files)     \
	     $(service_in_files)    \
	     $(appicon_DATA)        \
	     $(Multimedia_in_files) \
	     $(applications_DATA)   \
	     $(schema_DATA)	    \
	     $(iradio_DATA)	    \
	     $(playlists_in_files)  \
		generate-keys.sh

CLEANFILES = rhythmbox.desktop rhythmbox.keys GNOME_Rhythmbox.server org.gnome.Rhythmbox.service playlists.xml
