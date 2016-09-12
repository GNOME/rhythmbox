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
  'rb-alert-dialog.c',
  'rb-entry-view.c',
  'rb-property-view.c',
  'rb-search-entry.c',
  'rb-header.c',
  'rb-dialog.c',
  'rb-cell-renderer-pixbuf.c',
  'rb-cell-renderer-rating.c',
  'gossip-cell-renderer-expander.c',
  'rb-song-info.c',
  'rb-rating.c',
  'rb-rating-helper.c',
  'rb-segmented-bar.c',
  'rb-library-browser.c',
  'rb-query-creator.c',
  'rb-query-creator-properties.c',
  'rb-uri-dialog.c',
  'eggwrapbox.c',
  'eggwrapbox-enums.c',
  'rb-source-toolbar.c',
  'rb-fading-image.c',
  'rb-object-property-editor.c',
  'rb-import-dialog.c',
  'rb-button-bar.c',
  'rb-task-list-display.c',
  'rb-encoding-settings.c'
)

widget_c_args = [
  '-DGNOMELOCALEDIR="' + get_option('datadir') + '/locale"',
  '-DG_LOG_DOMAIN="Rhythmbox"',
]

widget_dependencies = [ libnotify ]
widget_link_args = ['-export-dynamic']
