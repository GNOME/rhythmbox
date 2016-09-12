sendto_plugin_dir = plugindir + '/sendto'

sendto_plugin_files = [
  'sendto.py']

install_data(sendto_plugin_files,
  install_dir: sendto_plugin_dir)

sendto_plugin_descriptor = custom_target('sendto-plugin-descriptor',
  input: 'sendto.plugin.in',
  output: 'sendto.plugin',
  command: [intltool_merge, '--quiet', '--desktop-style', meson.source_root() + '/po', '@INPUT@', '@OUTPUT@'],
  install: true,
  install_dir: sendto_plugin_dir)
