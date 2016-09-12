ipod_plugin_dir = plugindir + '/ipod'
ipod_plugin_data_dir = plugindatadir + '/ipod'

ipod_resources = gnome.compile_resources('ipod-resources',
  'ipod.gresource.xml',
  source_dir: meson.current_source_dir())

ipod_sources = [
  ipod_resources,
  'rb-ipod-db.c',
  'rb-ipod-helpers.c',
  'rb-ipod-static-playlist-source.c',
  'rb-ipod-source.c',
  'rb-ipod-plugin.c'
]

ipod_dependencies = [rhythmbox_core_dep, libgpod]

shared_library('ipod',
  ipod_sources,
  c_args: ['-DHAVE_CONFIG_H'],
  dependencies: ipod_dependencies,
  install_dir: ipod_plugin_dir)

ipod_plugin_descriptor = custom_target('ipod-plugin-descriptor',
  input: 'ipod.plugin.in',
  output: 'ipod.plugin',
  command: [intltool_merge, '--quiet', '--desktop-style', meson.source_root() + '/po', '@INPUT@', '@OUTPUT@'],
  install: true,
  install_dir: ipod_plugin_dir)
