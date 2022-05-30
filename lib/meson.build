lib_headers = files(
  'rb-async-copy.h',
  'rb-builder-helpers.h',
  'rb-chunk-loader.h',
  'rb-debug.h',
  'rb-file-helpers.h',
  'rb-gst-media-types.h',
  'rb-list-model.h',
  'rb-stock-icons.h',
  'rb-string-value-map.h',
  'rb-task-progress.h',
  'rb-task-progress-simple.h',
  'rb-util.h',
)

install_headers(lib_headers, subdir: 'rhythmbox/lib')

lib_sources = files(
  'rb-async-copy.c',
  'rb-async-queue-watch.c',
  'rb-builder-helpers.c',
  'rb-chunk-loader.c',
  'rb-cut-and-paste-code.c',
  'rb-debug.c',
  'rb-file-helpers.c',
  'rb-gst-media-types.c',
  'rb-list-model.c',
  'rb-missing-plugins.c',
  'rb-stock-icons.c',
  'rb-string-value-map.c',
  'rb-task-progress-simple.c',
  'rb-task-progress.c',
  'rb-text-helpers.c',
  'rb-tree-dnd.c',
  'rb-util.c',
)

lib_c_args = [
  '-DG_LOG_DOMAIN="Rhythmbox"',
  '-DGNOMELOCALEDIR="' + datadir + '/locale"',
  '-DSHARE_DIR="' + pkgdatadir + '"',
  '-DDATADIR="' + datadir + '"'
]

librb = static_library('rb', lib_sources,
  c_args: lib_c_args,
  include_directories: [configinc],
  dependencies: common_deps,
  install: false,
)

librb_dep = declare_dependency(
  link_with: librb,
  dependencies: common_deps,
  include_directories: [configinc, include_directories('.')],
)
