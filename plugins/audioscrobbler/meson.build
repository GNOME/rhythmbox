audioscrobbler_plugin_dir = plugindir + '/audioscrobbler'
audioscrobbler_plugin_data_dir = plugindatadir + '/audioscrobbler'

audioscrobbler_sources = [
  'rb-audioscrobbler-plugin.c',
  'rb-audioscrobbler-entry.c',
  'rb-audioscrobbler-profile-page.c',
  'rb-audioscrobbler-account.c',
  'rb-audioscrobbler-service.c',
  'rb-audioscrobbler-user.c',
  'rb-audioscrobbler.c',
  'rb-audioscrobbler-radio-source.c',
  'rb-audioscrobbler-radio-track-entry-type.c',
  'rb-audioscrobbler-play-order.c']

audioscrobbler_test_sources = [
  'rb-audioscrobbler-entry.c',
  'rb-audioscrobbler-radio-track-entry-type.c']

shared_module('audioscrobbler',
  audioscrobbler_sources,
  dependencies: [rhythmbox_core_dep, json_glib, totem_plparser],
  install: true,
  install_dir: audioscrobbler_plugin_dir)

audioscrobbler_plugin_descriptor = custom_target('audioscrobbler-plugin-descriptor',
  input: 'audioscrobbler.plugin.desktop.in',
  output: 'audioscrobbler.plugin',
  command: msgfmt_plugin_cmd,
  install: true,
  install_dir: audioscrobbler_plugin_dir)

install_data(
  'Last.fm-symbolic.svg',
  install_dir: audioscrobbler_plugin_data_dir + '/icons/hicolor/scalable/places')

install_data(
  'audioscrobbler-preferences.ui',
  'audioscrobbler-profile.ui',
  install_dir: audioscrobbler_plugin_data_dir)

libaudioscrobbler_test = library('audioscrobbler-test',
  audioscrobbler_test_sources,
  dependencies: rhythmbox_core_dep)

audioscrobbler_test_dep = declare_dependency(
  link_with: libaudioscrobbler_test,
  include_directories: include_directories('.')
)
