daap_plugin_dir = plugindir + '/daap'
daap_plugin_data_dir = plugindatadir + '/daap'

daap_resources = gnome.compile_resources('daap-resources',
  'daap.gresource.xml',
  source_dir: meson.current_source_dir())

daap_sources = [
  daap_resources,
  'rb-daap-container-record.c',
  'rb-daap-plugin.c',
  'rb-daap-record.c',
  'rb-daap-record-factory.c',
  'rb-daap-sharing.c',
  'rb-daap-source.c',
  'rb-daap-src.c',
  'rb-dmap-container-db-adapter.c',
  'rb-dacp-pairing-page.c',
  'rb-dacp-player.c',
  'rb-rhythmdb-dmap-db-adapter.c',
  'rb-rhythmdb-query-model-dmap-db-adapter.c',
]

daap_dependencies = [rhythmbox_core_dep, pango, libsecret, libdmapsharing4]
args = []

shared_module('daap',
  daap_sources,
  c_args: args,
  dependencies: daap_dependencies,
  install: true,
  install_dir: daap_plugin_dir)

daap_plugin_descriptor = custom_target('daap-plugin-descriptor',
  input: 'daap.plugin.desktop.in',
  output: 'daap.plugin',
  command: msgfmt_plugin_cmd,
  install: true,
  install_dir: daap_plugin_dir)
