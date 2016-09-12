power_manager_plugin_dir = plugindir + '/power-manager'
power_manager_plugin_data_dir = plugindatadir + '/power-manager'

power_manager_sources = [
  'rb-power-manager-plugin.c',
]

shared_module('power-manager',
  power_manager_sources,
  c_args: ['-DHAVE_CONFIG_H'],
  dependencies: [rhythmbox_core_dep],
  install: true,
  install_dir: power_manager_plugin_dir)

power_manager_plugin_descriptor = custom_target('power-manager-plugin-descriptor',
  input: 'power-manager.plugin.in',
  output: 'power-manager.plugin',
  command: [intltool_merge, '--quiet', '--desktop-style', meson.project_source_root() + '/po', '@INPUT@', '@OUTPUT@'],
  install: true,
  install_dir: power_manager_plugin_dir)
