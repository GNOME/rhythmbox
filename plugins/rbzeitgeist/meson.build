rbzeitgeist_plugin_dir = plugindir + '/rbzeitgeist'

rbzeitgeist_plugin_files = [
  'rbzeitgeist.py']

install_data(rbzeitgeist_plugin_files,
  install_dir: rbzeitgeist_plugin_dir)

rbzeitgeist_plugin_descriptor = custom_target('rbzeitgeist-plugin-descriptor',
  input: 'rbzeitgeist.plugin.desktop.in',
  output: 'rbzeitgeist.plugin',
  command: msgfmt_plugin_cmd,
  install: true,
  install_dir: rbzeitgeist_plugin_dir)
