widgets_headers = files(
  'rb-button-bar.h',
  'rb-cell-renderer-pixbuf.h',
  'rb-cell-renderer-rating.h',
  'rb-dialog.h',
  'rb-entry-view.h',
  'rb-fading-image.h',
  'rb-library-browser.h',
  'rb-property-view.h',
  'rb-rating.h',
  'rb-search-entry.h',
  'rb-segmented-bar.h',
  'rb-song-info.h',
  'rb-source-toolbar.h',
  'rb-uri-dialog.h',
)

install_headers(widgets_headers, subdir: 'rhythmbox/widgets')

widgets_sources = files(
  'eggwrapbox-enums.c',
  'eggwrapbox.c',
  'gossip-cell-renderer-expander.c',
  'nautilus-floating-bar.c',
  'rb-alert-dialog.c',
  'rb-button-bar.c',
  'rb-cell-renderer-pixbuf.c',
  'rb-cell-renderer-rating.c',
  'rb-dialog.c',
  'rb-encoding-settings.c',
  'rb-entry-view.c',
  'rb-fading-image.c',
  'rb-header.c',
  'rb-import-dialog.c',
  'rb-library-browser.c',
  'rb-object-property-editor.c',
  'rb-property-view.c',
  'rb-query-creator-properties.c',
  'rb-query-creator.c',
  'rb-rating-helper.c',
  'rb-rating.c',
  'rb-search-entry.c',
  'rb-segmented-bar.c',
  'rb-song-info.c',
  'rb-source-toolbar.c',
  'rb-task-list-display.c',
  'rb-uri-dialog.c',
)


widgets_c_args = [
  '-DGNOMELOCALEDIR="' + datadir + '/locale"',
  '-DG_LOG_DOMAIN="Rhythmbox"',
]

widgets_dependencies = [gtk, gdk_pixbuf, cairo, pango, gstreamer_pbutils, libsoup]

rbwidgets_lib = static_library('rbwidgets',
  widgets_sources,
  dependencies: widgets_dependencies + [librb_dep, rhythmdb_dep, rbmetadata_dep,
  rbbackends_dep, rbpodcast_dep, sources_dep],
  include_directories: [pluginsinc, shellinc, widgetsinc])

rbwidgets_dep = declare_dependency(
  link_with: rbwidgets_lib,
  dependencies: widgets_dependencies,
  include_directories: include_directories('.'),
)

# These are interactive UI tests.
test_rb_segmented_bar = executable('test-rb-segmented-bar', 'test-rb-segmented-bar.c',
  dependencies: [rbwidgets_dep, librb_dep],
  include_directories: [sourcesinc, rhythmdbinc])

test_uri_dialog_resources = gnome.compile_resources('test-uri-dialog-resources', 'test-uri-dialog.gresource.xml',
  source_dir: ['../data'])
test_uri_dialog = executable('test-uri-dialog', ['test-uri-dialog.c', test_uri_dialog_resources],
  dependencies: [rbwidgets_dep, librb_dep])
