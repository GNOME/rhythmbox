gstreamer_backend_sources = files(
  'rb-encoder-gst.c',
  'rb-player-gst-helper.c',
  'rb-player-gst.c',
  'rb-player-gst-xfade.c',
)

gstreamer_backend_c_args = [
  '-DGNOMELOCALEDIR="' + get_option('datadir') + '/locale"',
  '-DG_LOG_DOMAIN="Rhythmbox"',
]

gstreamer_backend_dependencies = [gdk_pixbuf]

gstreamer_backend_link_args = ['-export-dynamic']
