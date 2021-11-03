gstreamer_backend_headers = files(
  'rb-player-gst-helper.h'
)

gstreamer_backend_sources = files(
  'rb-encoder-gst.c',
  'rb-player-gst-helper.c',
  'rb-player-gst.c',
  'rb-player-gst-xfade.c',
)

gstreamer_backend_c_args = [
  '-DGNOMELOCALEDIR="' + datadir + '/locale"',
  '-DG_LOG_DOMAIN="Rhythmbox"',
]

gstreamer_backend_dependencies = [gstreamer_base, gstreamer_controller,
  gstreamer_plugins_base, gstreamer_tag]

rbbackendsgstreamer_lib = static_library('rbbackendsgstreamer',
  gstreamer_backend_sources,
  dependencies: [gstreamer_backend_dependencies, rhythmdb_dep, librb_dep],
  c_args: gstreamer_backend_c_args,
  include_directories: backends_inc,
)

rbbackendsgstreamer_dep = declare_dependency(
  link_with: rbbackendsgstreamer_lib,
  dependencies: gstreamer_backend_dependencies,
  include_directories: include_directories('.')
)
