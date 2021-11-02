mpris_plugin_dir = plugindir + '/mpris'

mpris_sources = [
  'rb-mpris-plugin.c']

mpris_c_args = [
  '-DGNOMELOCALEDIR="@0@"'.format(datadir / 'locale'),
  '-DG_LOG_DOMAIN="Rhythmbox"',
  '-DPIXMAP_DIR="@0@"'.format(datadir / 'pixmaps'),
  '-DSHARE_DIR="@0@"'.format(datadir),
  '-DDATADIR="@0@"'.format(pkgdatadir),
]

shared_module('mpris',
  mpris_sources,
  dependencies: [rhythmbox_core_dep],
  c_args: mpris_c_args,
  include_directories: pluginsinc,
  install: true,
  install_dir: mpris_plugin_dir)

mpris_plugin_descriptor = custom_target('mpris-plugin-descriptor',
  input: 'mpris.plugin.in',
  output: 'mpris.plugin',
  command: [intltool_merge, '--quiet', '--desktop-style', meson.project_source_root() + '/po', '@INPUT@', '@OUTPUT@'],
  install: true,
  install_dir: mpris_plugin_dir)
