pythonconsole_plugin_dir = plugindir + '/python-console'

pythonconsole_plugin_files = [
  'pythonconsole.py']

install_data(pythonconsole_plugin_files,
  install_dir: pythonconsole_plugin_dir)

pythonconsole_plugin_descriptor = custom_target('pythonconsole-plugin-descriptor',
  input: 'pythonconsole.plugin.desktop.in',
  output: 'pythonconsole.plugin',
  command: msgfmt_plugin_cmd,
  install: true,
  install_dir: pythonconsole_plugin_dir)
