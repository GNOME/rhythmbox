mpris_plugin_dir = plugindir + '/mpris'

mpris_sources = [
  'rb-mpris-plugin.c']

shared_library('mpris',
  mpris_sources,
  dependencies: [rhythmbox_core_dep],
  include_directories: pluginsinc,
  install_dir: mpris_plugin_dir)

mpris_plugin_descriptor = custom_target('mpris-plugin-descriptor',
  input: 'mpris.plugin.in',
  output: 'mpris.plugin',
  command: [intltool_merge, '--quiet', '--desktop-style', meson.source_root() + '/po', '@INPUT@', '@OUTPUT@'],
  install: true,
  install_dir: mpris_plugin_dir)
