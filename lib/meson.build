subdir('libmediaplayerid')

lib_headers = [
  'rb-builder-helpers.h',
  'rb-debug.h',
  'rb-file-helpers.h',
  'rb-list-model.h',
  'rb-stock-icons.h',
  'rb-string-value-map.h',
  'rb-util.h',
  'rb-task-progress.h',
  'rb-task-progress-simple.h'
]

install_headers(lib_headers, subdir: 'rhythmbox/lib')

lib_sources = [
  'rb-debug.c',
  'rb-file-helpers.c',
  'rb-builder-helpers.c',
  'rb-stock-icons.c',
  'rb-cut-and-paste-code.c',
  'rb-util.c',
  'rb-tree-dnd.c',
  'rb-string-value-map.c',
  'rb-async-queue-watch.c',
  'rb-text-helpers.c',
  'rb-gst-media-types.c',
  'rb-missing-plugins.c',
  'rb-async-copy.c',
  'rb-chunk-loader.c',
  'rb-task-progress.c',
  'rb-task-progress-simple.c',
  'rb-list-model.c'
]

lib_c_args = [
  '-DGNOMELOCALEDIR="' + get_option('datadir') + '/locale"',
  '-DSHARE_DIR="' + get_option('datadir') + '"',
  '-DDATADIR="' + get_option('datadir') + '"'
]

lib_dependencies = [
   gio, gio_unix, glib, gobject, gstreamer, gstreamer_pbutils, gtk, libpeas,
   libpeas_gtk, libxml
]

librb = static_library('rb', lib_sources,
  c_args: lib_c_args + ['-DG_LOG_DOMAIN="Rhythmbox"'],
  include_directories: [configinc],
  dependencies: lib_dependencies,
  install: true,
  link_args: ['-export-dynamic']
)

lib_dep = declare_dependency(
  link_whole: librb,
  link_with: librb,
  dependencies: lib_dependencies,
  include_directories: [configinc, libinc],
)
