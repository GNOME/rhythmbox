backends_inc = include_directories('.')

subdir('gstreamer')

backends_headers = files(
  'rb-encoder.h',
  'rb-player.h',
  'rb-player-gst-filter.h',
  'rb-player-gst-tee.h'
)

install_headers(backends_headers, subdir: 'rhythmbox/backends')

backends_sources = files(
  'rb-encoder.c',
  'rb-player.c',
  'rb-player-gst-filter.c',
  'rb-player-gst-tee.c',
)

backends_c_args = [
  '-DGNOMELOCALEDIR="' + datadir + '/locale"',
  '-DG_LOG_DOMAIN="Rhythmbox"',
]

backends_dependencies = [gstreamer_backend_dependencies]

rbbackends_lib = static_library('rbbackends',
  backends_sources,
  dependencies: [librb_dep, rbbackendsgstreamer_dep],
  c_args: backends_c_args,
  link_args: ['-export-dynamic'],
)

rbbackends_dep = declare_dependency(
  link_with: rbbackends_lib,
  dependencies: backends_dependencies,
  include_directories: include_directories('.'),
)
