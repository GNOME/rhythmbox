subdir('gstreamer')

backend_headers = [
  'rb-encoder.h',
  'rb-player.h',
  'rb-player-gst-filter.h',
  'rb-player-gst-tee.h'
]

install_headers(backend_headers, subdir: 'rhythmbox/backends')

backend_sources = files(
  'rb-encoder.c',
  'rb-player.c',
  'rb-player-gst-filter.c',
  'rb-player-gst-tee.c',
)

backend_c_args = [
  '-DGNOMELOCALEDIR="' + get_option('datadir') + '/locale"',
  '-DG_LOG_DOMAIN="Rhythmbox"',
]

backend_dependencies = []
backend_link_args = ['-export-dynamic']
