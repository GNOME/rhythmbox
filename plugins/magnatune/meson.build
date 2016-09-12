magnatune_plugin_dir = plugindir + '/magnatune'
magnatune_plugin_data_dir = plugindatadir + '/magnatune'

magnatune_plugin_files = [
  'MagnatuneSource.py',
  'DownloadAlbumHandler.py',
  'TrackListHandler.py',
  'MagnatuneAccount.py',
  'magnatune.py']

install_data(magnatune_plugin_files,
  install_dir: magnatune_plugin_dir)

magnatune_plugin_descriptor = custom_target('magnatune-plugin-descriptor',
  input: 'magnatune.plugin.in',
  output: 'magnatune.plugin',
  command: [intltool_merge, '--quiet', '--desktop-style', meson.source_root() + '/po', '@INPUT@', '@OUTPUT@'],
  install: true,
  install_dir: magnatune_plugin_dir)

install_data([
  'magnatune-loading.ui',
  'magnatune-popup.ui',
  'magnatune-prefs.ui',
  'magnatune-toolbar.ui',
  'magnatune_logo_color_small.png',
  'magnatune_logo_color_tiny.png'],
  install_dir: magnatune_plugin_data_dir)
