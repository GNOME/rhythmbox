webremote_plugin_dir = plugindir + '/webremote'
webremote_plugin_data_dir = plugindatadir + '/webremote'

webremote_plugin_files = [
  'webremote.py',
  'siphash.py',]

install_data(webremote_plugin_files,
  install_dir: webremote_plugin_dir)

webremote_plugin_descriptor = custom_target('webremote-plugin-descriptor',
  input: 'webremote.plugin.desktop.in',
  output: 'webremote.plugin',
  command: msgfmt_plugin_cmd,
  install: true,
  install_dir: webremote_plugin_dir)

install_data([
  'css/grids-responsive-min.css',
  'css/pure-min.css',
  'css/webremote.css'],
  install_dir: webremote_plugin_data_dir + '/css')

install_data('js/webremote.js',
  install_dir: webremote_plugin_data_dir + '/js')

install_data('webremote.html',
  install_dir: webremote_plugin_data_dir)

install_data('webremote-config.ui',
  install_dir: webremote_plugin_data_dir)
