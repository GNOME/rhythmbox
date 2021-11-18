mmkeys_plugin_dir = plugindir + '/mmkeys'

mmkeys_sources = [
  'rb-mmkeys-plugin.c']

shared_module('mmkeys',
  mmkeys_sources,
  dependencies: [rhythmbox_core_dep],
  c_args: ['-DHAVE_CONFIG_H'],
  include_directories: pluginsinc,
  install: true,
  install_dir: mmkeys_plugin_dir)

mmkeys_plugin_descriptor = custom_target('mmkeys-plugin-descriptor',
  input: 'mmkeys.plugin.desktop.in',
  output: 'mmkeys.plugin',
  command: msgfmt_plugin_cmd,
  install: true,
  install_dir: mmkeys_plugin_dir)
