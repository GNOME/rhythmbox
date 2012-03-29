
-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS =
INTROSPECTION_SCANNER_ARGS = --add-include-path=$(top_srcdir) --add-include-path=$(top_builddir)/bindings/gi --c-include=rhythmbox.h --warn-all
INTROSPECTION_COMPILER_ARGS = --includedir=$(top_srcdir)

mpid_introspection_sources = \
		lib/libmediaplayerid/mediaplayerid.h \
		lib/libmediaplayerid/mpid-device.c

rb_introspection_sources = \
		backends/rb-encoder.h \
		backends/rb-encoder.c \
		backends/rb-player-gst-filter.h \
		backends/rb-player-gst-filter.c \
		backends/rb-player-gst-tee.h \
		backends/rb-player-gst-tee.c \
		backends/rb-player.h \
		backends/rb-player.c \
		lib/rb-async-copy.h \
		lib/rb-async-copy.c \
		lib/rb-builder-helpers.h \
		lib/rb-builder-helpers.c \
		lib/rb-chunk-loader.h \
		lib/rb-chunk-loader.c \
		lib/rb-debug.h \
		lib/rb-debug.c \
		lib/rb-file-helpers.h \
		lib/rb-file-helpers.c \
		lib/rb-stock-icons.h \
		lib/rb-stock-icons.c \
		lib/rb-string-value-map.h \
		lib/rb-string-value-map.c \
		lib/rb-util.h \
		lib/rb-util.c \
		metadata/rb-ext-db.h \
		metadata/rb-ext-db.c \
		metadata/rb-ext-db-key.h \
		metadata/rb-ext-db-key.c \
		metadata/rb-metadata.h \
		metadata/rb-metadata-dbus-client.c \
		podcast/rb-podcast-manager.h \
		podcast/rb-podcast-manager.c \
		podcast/rb-podcast-parse.h \
		podcast/rb-podcast-parse.c \
		rhythmdb/rhythmdb.h \
		rhythmdb/rhythmdb.c \
		rhythmdb/rhythmdb-entry.h \
		rhythmdb/rhythmdb-entry-type.h \
		rhythmdb/rhythmdb-entry-type.c \
		rhythmdb/rhythmdb-property-model.h \
		rhythmdb/rhythmdb-property-model.c \
		rhythmdb/rhythmdb-query.c \
		rhythmdb/rhythmdb-query-model.h \
		rhythmdb/rhythmdb-query-model.c \
		rhythmdb/rhythmdb-query-result-list.h \
		rhythmdb/rhythmdb-query-result-list.c \
		rhythmdb/rhythmdb-query-results.h \
		rhythmdb/rhythmdb-query-results.c \
		rhythmdb/rhythmdb-import-job.h \
		rhythmdb/rhythmdb-import-job.c \
		rhythmdb/rhythmdb-song-entry-types.c \
		rhythmdb/rb-refstring.h \
		rhythmdb/rb-refstring.c \
		shell/rb-shell.h \
		shell/rb-shell.c \
		shell/rb-shell-player.h \
		shell/rb-shell-player.c \
		shell/rb-shell-preferences.h \
		shell/rb-shell-preferences.c \
		shell/rb-playlist-manager.h \
		shell/rb-playlist-manager.c \
		shell/rb-removable-media-manager.h \
		shell/rb-removable-media-manager.c \
		shell/rb-history.h \
		shell/rb-history.c \
		shell/rb-play-order.h \
		shell/rb-play-order.c \
		shell/rb-track-transfer-batch.c \
		shell/rb-track-transfer-batch.h \
		shell/rb-track-transfer-queue.c \
		shell/rb-track-transfer-queue.h \
		sources/rb-display-page.h \
		sources/rb-display-page.c \
		sources/rb-display-page-group.h \
		sources/rb-display-page-group.c \
		sources/rb-display-page-model.h \
		sources/rb-display-page-model.c \
		sources/rb-display-page-tree.h \
		sources/rb-display-page-tree.c \
		sources/rb-source.h \
		sources/rb-source.c \
		sources/rb-streaming-source.h \
		sources/rb-streaming-source.c \
		sources/rb-source-search.h \
		sources/rb-source-search.c \
		sources/rb-browser-source.h \
		sources/rb-browser-source.c \
		sources/rb-media-player-source.h \
		sources/rb-media-player-source.c \
		sources/rb-playlist-source.h \
		sources/rb-playlist-source.c \
		sources/rb-playlist-xml.h \
		sources/rb-auto-playlist-source.h \
		sources/rb-auto-playlist-source.c \
		sources/rb-static-playlist-source.h \
		sources/rb-static-playlist-source.c \
		sources/rb-source-search-basic.h \
		sources/rb-source-search-basic.c \
		sources/rb-device-source.h \
		sources/rb-device-source.c \
		sources/rb-transfer-target.h \
		sources/rb-transfer-target.c \
		widgets/rb-entry-view.h \
		widgets/rb-entry-view.c \
		widgets/rb-property-view.h \
		widgets/rb-property-view.c \
		widgets/rb-dialog.h \
		widgets/rb-dialog.c \
		widgets/rb-cell-renderer-pixbuf.h \
		widgets/rb-cell-renderer-pixbuf.c \
		widgets/rb-cell-renderer-rating.h \
		widgets/rb-cell-renderer-rating.c \
		widgets/rb-rating.h \
		widgets/rb-rating.c \
		widgets/rb-library-browser.h \
		widgets/rb-library-browser.c \
		widgets/rb-search-entry.h \
		widgets/rb-search-entry.c \
		widgets/rb-source-toolbar.h \
		widgets/rb-source-toolbar.c \
		widgets/rb-segmented-bar.h \
		widgets/rb-segmented-bar.c \
		widgets/rb-song-info.h \
		widgets/rb-song-info.c \
		widgets/rb-uri-dialog.h \
		widgets/rb-uri-dialog.c

MPID-3.0.gir: $(top_builddir)/shell/librhythmbox-core.la
MPID_3_0_gir_INCLUDES = GObject-2.0
MPID_3_0_gir_CFLAGS = $(RHYTHMBOX_CFLAGS_NOWARN) $(GUDEV_CFLAGS) -I$(top_srcdir)
MPID_3_0_gir_LIBS = $(top_builddir)/shell/librhythmbox-core.la
MPID_3_0_gir_FILES = $(addprefix $(top_srcdir)/,$(mpid_introspection_sources))
MPID_3_0_gir_PACKAGES = gobject-2.0
MPID_3_0_gir_SCANNERFLAGS = --symbol-prefix mpid_

RB-3.0.gir: $(top_builddir)/shell/librhythmbox-core.la MPID-3.0.gir
RB_3_0_gir_INCLUDES = GObject-2.0 Gio-2.0 Gtk-3.0 Gst-0.10 GstPbutils-0.10 libxml2-2.0
RB_3_0_gir_CFLAGS = $(RHYTHMBOX_CFLAGS_NOWARN) $(TOTEM_PLPARSER_CFLAGS) $(DBUS_CFLAGS) $(GUDEV_CFLAGS) -I$(top_srcdir)
RB_3_0_gir_LIBS = $(top_builddir)/shell/librhythmbox-core.la
RB_3_0_gir_FILES = $(addprefix $(top_srcdir)/,$(rb_introspection_sources))
RB_3_0_gir_PACKAGES = gobject-2.0 gio-2.0 gtk+-3.0 gstreamer-0.10 libxml-2.0
RB_3_0_gir_SCANNERFLAGS = --symbol-prefix rb_ --identifier-prefix RB --accept-unprefixed --include-uninstalled=$(builddir)/MPID-3.0.gir

INTROSPECTION_GIRS += MPID-3.0.gir RB-3.0.gir

girdir = $(datadir)/gir-1.0
dist_gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)
CLEANFILES = $(dist_gir_DATA) $(typelib_DATA)
