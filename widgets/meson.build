widget_headers = [
  'rb-alert-dialog.h',
  'rb-entry-view.h',
  'rb-property-view.h',
  'rb-dialog.h',
  'rb-cell-renderer-pixbuf.h',
  'rb-cell-renderer-rating.h',
  'rb-rating.h',
  'rb-library-browser.h',
  'rb-segmented-bar.h',
  'rb-song-info.h',
  'rb-source-toolbar.h',
  'rb-uri-dialog.h',
  'rb-fading-image.h',
  'rb-object-property-editor.h',
  'rb-import-dialog.h',
  'rb-button-bar.h',
  'rb-task-list-display.h',
  'rb-encoding-settings.h'
]

install_headers(widget_headers, subdir: 'rhythmbox/widgets')

widget_sources = files(
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


widget_c_args = [
  '-DGNOMELOCALEDIR="' + get_option('datadir') + '/locale"',
  '-DG_LOG_DOMAIN="Rhythmbox"',
]

widget_dependencies = [ libnotify ]
widget_link_args = ['-export-dynamic']
