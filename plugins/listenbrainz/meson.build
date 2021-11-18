listenbrainz_plugin_dir = plugindir + '/listenbrainz'
listenbrainz_plugin_data_dir = plugindatadir + '/listenbrainz'

listenbrainz_plugin_files = [
  'client.py',
  'listenbrainz.py',
  'queue.py',
  'settings.py',
]

install_data(listenbrainz_plugin_files,
  install_dir: listenbrainz_plugin_dir)

install_data(['settings.ui'],
  install_dir: listenbrainz_plugin_data_dir)

listenbrainz_plugin_descriptor = custom_target('listenbrainz-plugin-descriptor',
  input: 'listenbrainz.plugin.desktop.in',
  output: 'listenbrainz.plugin',
  command: msgfmt_plugin_cmd,
  install: true,
  install_dir: listenbrainz_plugin_dir)
