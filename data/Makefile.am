SUBDIRS = ui icons

@INTLTOOL_XML_RULE@
@INTLTOOL_DESKTOP_RULE@

Multimediadir = $(datadir)/applications
Multimedia_in_files = org.gnome.Rhythmbox3.desktop.in rhythmbox-device.desktop.in
Multimedia_DATA = $(Multimedia_in_files:.desktop.in=.desktop)

playlists_in_files = playlists.xml.in
noinst_DATA = playlists.xml

man_MANS = rhythmbox.1 rhythmbox-client.1

profilesdir = $(datadir)/rhythmbox
profiles_DATA = rhythmbox.gep

# GSettings schemas
# (summaries and descriptions are not translated, so no .xml.in)
gsettings_SCHEMAS = org.gnome.rhythmbox.gschema.xml

@GSETTINGS_RULES@

# compile schemas for running from the source tree
gschemas.compiled: $(gsettings_SCHEMAS:.xml=.valid)
	$(AM_V_GEN) $(GLIB_COMPILE_SCHEMAS) --targetdir=. .

all-local: gschemas.compiled

# Dbus service file
servicedir = $(datadir)/dbus-1/services
service_in_files = org.gnome.Rhythmbox3.service.in
service_DATA = $(service_in_files:.service.in=.service)

appdatadir = $(datadir)/metainfo
appdata_DATA = $(appdata_in_files:.xml.in=.xml)
appdata_in_files = org.gnome.Rhythmbox3.appdata.xml.in

# Rule to make the service file with bindir expanded
$(service_DATA): $(service_in_files) Makefile
	@sed -e "s|\@bindir\@|$(bindir)|" $< > $@

# Rule to update the icon cache after installing the icon
gtk_update_icon_cache = gtk-update-icon-cache -f -t $(datadir)/icons/hicolor

install-data-hook: update-icon-cache
uninstall-hook: update-icon-cache
update-icon-cache:
	@-if test -z "$(DESTDIR)"; then \
		echo "Updating Gtk icon cache."; \
		$(gtk_update_icon_cache); \
	else \
		echo "*** Icon cache not updated.  After (un)install, run this:"; \
		echo "***   $(gtk_update_icon_cache)"; \
	fi

EXTRA_DIST = $(service_in_files)    \
	     $(gsettings_SCHEMAS)   \
	     $(man_MANS)            \
	     $(playlists_in_files)  \
	     $(profiles_DATA)       \
	     $(appdata_in_files)    \
	     org.gnome.Rhythmbox3.desktop.in.in \
	     rhythmbox-device.desktop.in.in

CLEANFILES = 					\
	org.gnome.Rhythmbox3.desktop			\
	rhythmbox-device.desktop		\
	org.gnome.Rhythmbox3.service		\
	playlists.xml				\
	org.gnome.Rhythmbox3.desktop.in			\
	rhythmbox-device.desktop.in		\
	gschemas.compiled			\
	$(appdata_DATA)
