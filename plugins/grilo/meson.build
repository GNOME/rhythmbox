grilo_plugin_dir = plugindir + '/grilo'
grilo_plugin_data_dir = plugindatadir + '/grilo'

grilo_sources = [
  'rb-grilo-plugin.c',
  'rb-grilo-source.c'
]

grilo_dependencies = [rhythmbox_core_dep, grilo]

shared_library('grilo',
  grilo_sources,
  c_args: ['-DHAVE_CONFIG_H'],
  dependencies: grilo_dependencies,
  install_dir: grilo_plugin_dir)

grilo_plugin_descriptor = custom_target('grilo-plugin-descriptor',
  input: 'grilo.plugin.in',
  output: 'grilo.plugin',
  command: [intltool_merge, '--quiet', '--desktop-style', meson.source_root() + '/po', '@INPUT@', '@OUTPUT@'],
  install: true,
  install_dir: grilo_plugin_dir)
