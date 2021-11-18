replaygain_plugin_dir = plugindir + '/replaygain'
replaygain_plugin_data_dir = plugindatadir + '/replaygain'

replaygain_plugin_files = [
  'replaygain.py',
  'player.py',
  'config.py']

install_data(replaygain_plugin_files,
  install_dir: replaygain_plugin_dir)

replaygain_plugin_descriptor = custom_target('replaygain-plugin-descriptor',
  input: 'replaygain.plugin.desktop.in',
  output: 'replaygain.plugin',
  command: msgfmt_plugin_cmd,
  install: true,
  install_dir: replaygain_plugin_dir)

install_data('replaygain-prefs.ui',
  install_dir: replaygain_plugin_data_dir)
