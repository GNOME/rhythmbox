iradio_plugin_dir = plugindir + '/iradio'
iradio_plugin_data_dir = plugindatadir + '/iradio'

iradio_resources = gnome.compile_resources('iradio-resources',
  'iradio.gresource.xml',
  source_dir: meson.current_source_dir())

iradio_sources = [
  iradio_resources,
  'rb-iradio-plugin.c',
  'rb-iradio-source.c',
  'rb-iradio-source-search.c',
  'rb-station-properties-dialog.c',
]

iradio_dependencies = [rhythmbox_core_dep]

shared_module('iradio',
  iradio_sources,
  dependencies: iradio_dependencies,
  install: true,
  install_dir: iradio_plugin_dir)

iradio_plugin_descriptor = custom_target('iradio-plugin-descriptor',
  input: 'iradio.plugin.desktop.in',
  output: 'iradio.plugin',
  command: msgfmt_plugin_cmd,
  install: true,
  install_dir: iradio_plugin_dir)
