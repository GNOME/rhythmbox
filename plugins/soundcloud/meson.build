soundcloud_plugin_dir = plugindir + '/soundcloud'
soundcloud_plugin_data_dir = plugindatadir + '/soundcloud'

soundcloud_plugin_files = [
  'soundcloud.py']

install_data(soundcloud_plugin_files,
  install_dir: soundcloud_plugin_dir)

soundcloud_plugin_descriptor = custom_target('soundcloud-plugin-descriptor',
  input: 'soundcloud.desktop.plugin.in',
  output: 'soundcloud.plugin',
  command: msgfmt_plugin_cmd,
  install: true,
  install_dir: soundcloud_plugin_dir)

install_data(['soundcloud.ui', 'powered-by-soundcloud.png'],
  install_dir: soundcloud_plugin_data_dir)

install_data(['icons/hicolor/scalable/places/soundcloud-symbolic.svg'],
  install_dir: soundcloud_plugin_data_dir + '/icons/hicolor/scalable/places')
