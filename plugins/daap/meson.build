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

daap_dependencies = [rhythmbox_core_dep, libdmapsharing]

if use_libsecret
  daap_dependencies += libsecret
endif

shared_library('daap',
  daap_sources,
  c_args: ['-DHAVE_CONFIG_H'],
  dependencies: daap_dependencies,
  install_dir: daap_plugin_dir)

daap_plugin_descriptor = custom_target('daap-plugin-descriptor',
  input: 'daap.plugin.in',
  output: 'daap.plugin',
  command: [intltool_merge, '--quiet', '--desktop-style', meson.source_root() + '/po', '@INPUT@', '@OUTPUT@'],
  install: true,
  install_dir: daap_plugin_dir)
