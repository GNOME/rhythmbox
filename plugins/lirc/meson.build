lirc_plugin_dir = plugindir + '/rblirc'
lirc_plugin_data_dir = plugindatadir + '/rblirc'

lirc_sources = [
  'rb-lirc-plugin.c'
]

shared_module('rblirc',
  lirc_sources,
  dependencies: [rhythmbox_core_dep, lirc],
  install_dir: lirc_plugin_dir,
  install: true)

lirc_plugin_descriptor = custom_target('lirc-plugin-descriptor',
  input: 'rblirc.plugin.desktop.in',
  output: 'rblirc.plugin',
  command: msgfmt_plugin_cmd,
  install: true,
  install_dir: lirc_plugin_dir)

install_data('rhythmbox_lirc_default',
  install_dir: lirc_plugin_data_dir)
