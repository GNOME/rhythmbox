android_plugin_dir = plugindir + '/android'

android_resources = gnome.compile_resources('android-resources',
  'android.gresource.xml',
  source_dir: meson.current_source_dir())

android_sources = [
  android_resources,
  'rb-android-plugin.c',
  'rb-android-source.c']

shared_module('android',
  android_sources,
  dependencies: [rhythmbox_core_dep, gudev],
  include_directories: pluginsinc,
  install: true,
  install_dir: android_plugin_dir)

android_plugin_descriptor = custom_target('android-plugin-descriptor',
  input: 'android.plugin.desktop.in',
  output: 'android.plugin',
  command: msgfmt_plugin_cmd,
  install: true,
  install_dir: android_plugin_dir)
