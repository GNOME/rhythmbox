notification_plugin_dir = plugindir + '/notification'

notification_sources = [
  'rb-notification-plugin.c']

shared_module('notification',
  notification_sources,
  dependencies: [rhythmbox_core_dep, libnotify, pango],
  include_directories: pluginsinc,
  install: true,
  install_dir: notification_plugin_dir)

notification_plugin_descriptor = custom_target('notification-plugin-descriptor',
  input: 'notification.plugin.desktop.in',
  output: 'notification.plugin',
  command: msgfmt_plugin_cmd,
  install: true,
  install_dir: notification_plugin_dir)
