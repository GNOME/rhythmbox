dbus_media_server_plugin_dir = plugindir + '/dbus-media-server'
dbus_media_server_plugin_data_dir = plugindatadir + '/dbus-media-server'

dbus_media_server_sources = [
  'rb-dbus-media-server-plugin.c'
]

shared_module('dbus-media-server',
  dbus_media_server_sources,
  dependencies: [rhythmbox_core_dep],
  install: true,
  install_dir: dbus_media_server_plugin_dir)

dbus_media_server_plugin_descriptor = custom_target('dbus-media-server-plugin-descriptor',
  input: 'dbus-media-server.plugin.desktop.in',
  output: 'dbus-media-server.plugin',
  command: msgfmt_plugin_cmd,
  install: true,
  install_dir: dbus_media_server_plugin_dir)
