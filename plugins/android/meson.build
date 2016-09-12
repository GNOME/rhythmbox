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
  c_args: ['-DHAVE_CONFIG_H'],
  dependencies: [rhythmbox_core_dep, gudev],
  include_directories: pluginsinc,
  install: true,
  install_dir: android_plugin_dir)

android_plugin_descriptor = custom_target('android-plugin-descriptor',
  input: 'android.plugin.in',
  output: 'android.plugin',
  command: [intltool_merge, '--quiet', '--desktop-style', meson.project_source_root() + '/po', '@INPUT@', '@OUTPUT@'],
  install: true,
  install_dir: android_plugin_dir)
