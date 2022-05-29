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

shared_module('ipod',
  ipod_sources,
  dependencies: ipod_dependencies,
  install: true,
  install_dir: ipod_plugin_dir)

ipod_plugin_descriptor = custom_target('ipod-plugin-descriptor',
  input: 'ipod.plugin.desktop.in',
  output: 'ipod.plugin',
  command: msgfmt_plugin_cmd,
  install: true,
  install_dir: ipod_plugin_dir)

if check.found()
    test('test-ipod',
    executable('test-ipod',
      ['test-ipod.c', 'rb-ipod-helpers.c'],
      dependencies: [rhythmbox_core_dep, libgpod, check]))
endif
