notification_plugin_dir = plugindir + '/notification'

notification_sources = [
  'rb-notification-plugin.c']

shared_module('notification',
  notification_sources,
  dependencies: [rhythmbox_core_dep, libnotify],
  c_args: ['-DHAVE_CONFIG_H'],
  include_directories: pluginsinc,
  install: true,
  install_dir: notification_plugin_dir)

notification_plugin_descriptor = custom_target('notification-plugin-descriptor',
  input: 'notification.plugin.in',
  output: 'notification.plugin',
  command: [intltool_merge, '--quiet', '--desktop-style', meson.project_source_root() + '/po', '@INPUT@', '@OUTPUT@'],
  install: true,
  install_dir: notification_plugin_dir)
