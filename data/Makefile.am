## arch-tag: Automake rules for various data files

SUBDIRS = ui glade art

@INTLTOOL_XML_RULE@
@INTLTOOL_DESKTOP_RULE@

Multimediadir = $(datadir)/applications
Multimedia_in_files = rhythmbox.desktop.in
Multimedia_DATA = $(Multimedia_in_files:.desktop.in=.desktop)

appicon_DATA = rhythmbox.png
appicondir = $(datadir)/icons/hicolor/48x48/apps

schemadir = @GCONF_SCHEMA_FILE_DIR@
schema_DATA = rhythmbox.schemas

iradio-initial.xml:

iradio_DATA = iradio-initial.pls
iradiodir = $(datadir)/rhythmbox

playlists_in_files = playlists.xml.in
playlists_DATA = playlists.xml
playlistsdir = $(datadir)/rhythmbox

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

# Rule to update the icon cache after installing the icon
gtk_update_icon_cache = gtk-update-icon-cache -f -t $(datadir)/icons/hicolor
install-data-hook:
	@-if test -z "$(DESTDIR)"; then \
		echo "Updating Gtk icon cache."; \
		$(gtk_update_icon_cache); \
	else \
		echo "*** Icon cache not updated.  After install, run this:"; \
		echo "***   $(gtk_update_icon_cache)"; \
	fi

EXTRA_DIST = $(service_in_files)    \
	     $(appicon_DATA)        \
	     $(schema_DATA)	    \
	     $(iradio_DATA)	    \
	     $(playlists_in_files)  \
	     rhythmbox.desktop.in.in

CLEANFILES = rhythmbox.desktop org.gnome.Rhythmbox.service playlists.xml rhythmbox.desktop.in
