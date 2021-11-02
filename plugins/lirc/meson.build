lirc_plugin_dir = plugindir + '/rblirc'
lirc_plugin_data_dir = plugindatadir + '/rblirc'

lirc_sources = [
  'rb-lirc-plugin.c'
]

shared_module('rblirc',
  lirc_sources,
  c_args: ['-DHAVE_CONFIG_H'],
  dependencies: [rhythmbox_core_dep, lirc],
  install_dir: lirc_plugin_dir,
  install: true)

lirc_plugin_descriptor = custom_target('lirc-plugin-descriptor',
  input: 'rblirc.plugin.in',
  output: 'rblirc.plugin',
  command: [intltool_merge, '--quiet', '--desktop-style', meson.project_source_root() + '/po', '@INPUT@', '@OUTPUT@'],
  install: true,
  install_dir: lirc_plugin_dir)

install_data('rhythmbox_lirc_default',
  install_dir: lirc_plugin_data_dir)
