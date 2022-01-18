fmradio_plugin_dir = plugindir / 'fmradio'
fmradio_plugin_data_dir = plugindatadir / 'fmradio'

fmradio_resources = gnome.compile_resources('fmradio-resources',
  'fmradio.gresource.xml',
  source_dir: meson.current_source_dir())

fmradio_sources = [
  fmradio_resources,
  'rb-fm-radio-plugin.c',
  'rb-fm-radio-source.c',
  'rb-fm-radio-gst-src.c',
  'rb-radio-tuner-v4l2.c'
]

shared_module('fmradio',
  fmradio_sources,
  dependencies: [rhythmbox_core_dep],
  install: true,
  install_dir: fmradio_plugin_dir)

fmradio_plugin_descriptor = custom_target('fmradio-plugin-descriptor',
  input: 'fmradio.plugin.desktop.in',
  output: 'fmradio.plugin',
  command: msgfmt_plugin_cmd,
  install: true,
  install_dir: fmradio_plugin_dir)
