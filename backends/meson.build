backends_headers = files(
  'rb-encoder.h',
  'rb-player.h',
  'rb-player-gst-filter.h',
  'rb-player-gst-tee.h',

  'gstreamer/rb-player-gst-helper.h',
)

install_headers(backends_headers, subdir: 'rhythmbox/backends')

backends_sources = files(
  'rb-encoder.c',
  'rb-player.c',
  'rb-player-gst-filter.c',
  'rb-player-gst-tee.c',

  'gstreamer/rb-encoder-gst.c',
  'gstreamer/rb-player-gst-helper.c',
  'gstreamer/rb-player-gst.c',
  'gstreamer/rb-player-gst-multi.c',
)

backends_c_args = [
  '-DGNOMELOCALEDIR="' + datadir + '/locale"',
  '-DG_LOG_DOMAIN="Rhythmbox"',
]

rbbackends_lib = static_library('rbbackends',
  backends_sources,
  dependencies: [common_deps, librb_dep, rhythmdb_dep],
  c_args: backends_c_args,
  include_directories: backendsinc,
)

rbbackends_dep = declare_dependency(
  link_with: rbbackends_lib,
  dependencies: common_deps,
  include_directories: backendsinc,
)
