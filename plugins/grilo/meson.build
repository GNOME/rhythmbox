grilo_plugin_dir = plugindir + '/grilo'
grilo_plugin_data_dir = plugindatadir + '/grilo'

grilo_sources = [
  'rb-grilo-plugin.c',
  'rb-grilo-source.c'
]

grilo_dependencies = [rhythmbox_core_dep, grilo]

shared_module('grilo',
  grilo_sources,
  dependencies: grilo_dependencies,
  install: true,
  install_dir: grilo_plugin_dir)

grilo_plugin_descriptor = custom_target('grilo-plugin-descriptor',
  input: 'grilo.plugin.desktop.in',
  output: 'grilo.plugin',
  command: msgfmt_plugin_cmd,
  install: true,
  install_dir: grilo_plugin_dir)
