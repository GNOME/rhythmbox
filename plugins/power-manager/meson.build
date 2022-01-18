power_manager_plugin_dir = plugindir + '/power-manager'
power_manager_plugin_data_dir = plugindatadir + '/power-manager'

power_manager_sources = [
  'rb-power-manager-plugin.c',
]

shared_module('power-manager',
  power_manager_sources,
  dependencies: [rhythmbox_core_dep],
  install: true,
  install_dir: power_manager_plugin_dir)

power_manager_plugin_descriptor = custom_target('power-manager-plugin-descriptor',
  input: 'power-manager.plugin.desktop.in',
  output: 'power-manager.plugin',
  command: msgfmt_plugin_cmd,
  install: true,
  install_dir: power_manager_plugin_dir)
