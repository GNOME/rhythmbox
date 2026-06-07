gpodder_plugin_dir = plugindir + '/gpodder'
gpodder_plugin_data_dir = plugindatadir + '/gpodder'

gpodder_plugin_files = [
  'gpodder.py',
  'sync.py'
]

install_data(gpodder_plugin_files,
  install_dir: gpodder_plugin_dir)

gpodder_plugin_descriptor = custom_target('gpodder-plugin-descriptor',
  input: 'gpodder.plugin.desktop.in',
  output: 'gpodder.plugin',
  command: msgfmt_plugin_cmd,
  install: true,
  install_dir: gpodder_plugin_dir)

install_data('gpodder-prefs.ui',
  install_dir: gpodder_plugin_data_dir)

