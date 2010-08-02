AUTOMAKE_OPTIONS = 1.6

DOC_MODULE=rhythmbox
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.sgml
DOC_SOURCE_DIR= ../../


# Extra options to pass to gtkdoc-scangobj. Not normally needed.
SCANGOBJ_OPTIONS=

# Extra options to supply to gtkdoc-scan.
# e.g. SCAN_OPTIONS=--deprecated-guards="GTK_DISABLE_DEPRECATED"
SCAN_OPTIONS=

# Extra options to supply to gtkdoc-mkdb.
MKDB_OPTIONS=--sgml-mode --output-format=xml

# Extra options to supply to gtkdoc-mktmpl
# e.g. MKTMPL_OPTIONS=--only-section-tmpl
MKTMPL_OPTIONS=

# Extra options to supply to gtkdoc-fixref. Not normally needed.
# e.g. FIXXREF_OPTIONS=--extra-dir=../gdk-pixbuf/html --extra-dir=../gdk/html
FIXXREF_OPTIONS=

# Used for dependencies. The docs will be rebuilt if any of these change.
HFILE_GLOB=$(top_srcdir)/lib/*.h
CFILE_GLOB=$(top_srcdir)/lib/*.c

# Header files to ignore when scanning.
# - imported code
# - generated code
# - plugins (no API there)
IGNORE_HFILES= \
	config.h \
	eel-gconf-extensions.h \
	eggdesktopfile.h \
	eggsmclient-private.h \
	eggsmclient.h \
	gseal-gtk-compat.h \
	md5.h \
	mkdtemp.h \
	rb-cut-and-paste-code.h \
	rb-marshal.h \
	rhythmdb-private.h \
	\
	rb-shell-glue.h \
	rb-shell-binding.h \
	rb-shell-player-glue.h \
	rb-shell-player-binding.h \
	rb-playlist-manager-glue.h \
	rb-play-order-linear.h \
	rb-play-order-linear-loop.h \
	rb-play-order-queue.h \
	rb-play-order-random-by-age-and-rating.h \
	rb-play-order-random-by-age.h \
	rb-play-order-random-by-rating.h \
	rb-play-order-random-equal-weights.h \
	rb-play-order-shuffle.h \
	\
	npapi.h \
	npruntime.h \
	npupp.h \
	rb-feed-podcast-properties-dialog.h \
	rb-podcast-manager.h \
	rb-podcast-parse.h \
	rb-podcast-properties-dialog.h \
	rb-podcast-source.h \
	\
	gossip-cell-renderer-expander.h \
	rb-query-creator-private.h \
	\
	rb-playlist-xml.h \
	rb-sync-settings.h \
	rb-sync-settings-ui.h \
	rb-sync-state.h \
	rb-sync-state-ui.h \
	\
	rb-audiocd-source.h \
	sj-error.h \
	sj-metadata-getter.h \
	sj-metadata-gvfs.h \
	sj-metadata-marshal.h \
	sj-metadata-musicbrainz.h \
	sj-metadata-musicbrainz3.h \
	sj-metadata.h \
	sj-structures.h \
	rb-audioscrobbler-entry.h \
	rb-lastfm-play-order.h \
	rb-audioscrobbler.h \
	rb-lastfm-source.h \
	rb-recorder.h \
	rb-playlist-source-recorder.h \
	rb-daap-record.h \
	rb-rhythmdb-query-model-dmap-db-adapter.h \
	rb-dmap-container-db-adapter.h \
	rb-daap-dialog.h \
	rb-rhythmdb-dmap-db-adapter.h \
	rb-daap-glue.h \
	rb-daap-src.h \
	rb-daap-plugin.h \
	rb-daap-source.h \
	rb-daap-sharing.h \
	rb-daap-record-factory.h \
	rb-daap-container-record.h \
	rb-fm-radio-source.h \
	rb-radio-tuner.h \
	rb-psp-source.h \
	rb-generic-player-source.h \
	rb-generic-player-playlist-source.h \
	rb-nokia770-source.h \
	rb-ipod-static-playlist-source.h \
	rb-ipod-source.h \
	rb-ipod-helpers.h \
	rb-ipod-db.h \
	rb-station-properties-dialog.h \
	rb-iradio-source-search.h \
	rb-iradio-source.h \
	rb-mtp-thread.h \
	rb-mtp-source.h \
	rb-static-icon-plugin.h \
	rb-tray-icon-gtk.h \
	rb-vis-widget.h





# Images to copy into HTML directory.
# e.g. HTML_IMAGES=$(top_srcdir)/gtk/stock-icons/stock_about_24.png
HTML_IMAGES=

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
# e.g. content_files=running.sgml building.sgml changes-2.0.sgml
content_files=

# SGML files where gtk-doc abbrevations (#GtkWidget) are expanded
# These files must be listed here *and* in content_files
# e.g. expand_content_files=running.sgml
expand_content_files=



INCLUDES=\
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-I$(top_builddir)/lib \
	-I$(top_srcdir)/sources \
	-I$(top_srcdir)/shell \
	-I$(top_srcdir)/rhythmdb \
	-I$(top_srcdir)/metadata \
	-I$(top_srcdir)/widgets \
	-I$(top_srcdir)/lib \
	-I$(top_srcdir)/lib/libmediaplayerid \
	-I$(top_srcdir)/plugins \
	-I$(top_srcdir)/backends \
	$(DBUS_CFLAGS) \
	$(IPOD_CFLAGS) \
	$(RHYTHMBOX_CFLAGS)
	
if ENABLE_PYTHON
BINDING_LIBS = $(top_builddir)/bindings/python/rb.la
MORE_GTKDOC_LIBS = $(PYTHON_LIBS)
endif

GTKDOC_LIBS=\
	$(top_builddir)/shell/librhythmbox-core.la        \
	$(BINDING_LIBS)					\
	$(TOTEM_PLPARSER_LIBS)				\
	$(HAL_LIBS)					\
	$(DBUS_LIBS)					\
	$(RHYTHMBOX_LIBS)				\
	$(MORE_GTKDOC_LIBS)

include $(top_srcdir)/gtk-doc.make

