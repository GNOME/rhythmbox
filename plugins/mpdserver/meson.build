mpdserver_plugin_dir = plugindir + '/mpdserver'
mpdserver_plugin_data_dir = plugindatadir + '/mpdserver'

mpdserver_plugin_files = [
  'mpdserver.py']

install_data(mpdserver_plugin_files,
  install_dir: mpdserver_plugin_dir)

mpdserver_plugin_descriptor = custom_target('mpdserver-plugin-descriptor',
  input: 'mpdserver.plugin.desktop.in',
  output: 'mpdserver.plugin',
  command: msgfmt_plugin_cmd,
  install: true,
  install_dir: mpdserver_plugin_dir)

install_data('mpdserver-config.ui',
  install_dir: mpdserver_plugin_data_dir)
