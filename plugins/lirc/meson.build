lirc_plugin_dir = plugindir + '/lirc'
lirc_plugin_data_dir = plugindatadir + '/lirc'

lirc_sources = [
  'rb-lirc-plugin.c'
]

shared_library('lirc',
  lirc_sources,
  c_args: ['-DHAVE_CONFIG_H'],
  dependencies: [rhythmbox_core_dep, lirc],
  install_dir: lirc_plugin_dir)

lirc_plugin_descriptor = custom_target('lirc-plugin-descriptor',
  input: 'rblirc.plugin.in',
  output: 'rblirc.plugin',
  command: [intltool_merge, '--quiet', '--desktop-style', meson.source_root() + '/po', '@INPUT@', '@OUTPUT@'],
  install: true,
  install_dir: lirc_plugin_dir)

install_data('rhythmbox_lirc_default',
  install_dir: lirc_plugin_data_dir)
