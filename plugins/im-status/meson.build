im_status_plugin_dir = plugindir + '/im-status'

im_status_plugin_files = [
  'im-status.py']

install_data(im_status_plugin_files,
  install_dir: im_status_plugin_dir)

im_status_plugin_descriptor = custom_target('im-status-plugin-descriptor',
  input: 'im-status.plugin.desktop.in',
  output: 'im-status.plugin',
  command: msgfmt_plugin_cmd,
  install: true,
  install_dir: im_status_plugin_dir)
