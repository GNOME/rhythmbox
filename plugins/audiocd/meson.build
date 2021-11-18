audiocd_plugin_dir = plugindir + '/audiocd'

audiocd_resources = gnome.compile_resources('audiocd-resources',
  'audiocd.gresource.xml',
  source_dir: meson.current_source_dir())

audiocd_sources = [
  audiocd_resources,
  'rb-audiocd-info.c',
  'rb-audiocd-plugin.c',
  'rb-audiocd-source.c',
  'rb-musicbrainz-lookup.c']

shared_module('audiocd',
  audiocd_sources,
  dependencies: [rhythmbox_core_dep, totem_plparser],
  include_directories: pluginsinc,
  install: true,
  install_dir: audiocd_plugin_dir)

audiocd_plugin_descriptor = custom_target('audiocd-plugin-descriptor',
  input: 'audiocd.plugin.desktop.in',
  output: 'audiocd.plugin',
  command: msgfmt_plugin_cmd,
  install: true,
  install_dir: audiocd_plugin_dir)

executable('test-cd',
  'test-cd.c',
  'rb-audiocd-info.c',
  'rb-musicbrainz-lookup.c',
  dependencies: [rhythmbox_core_dep, totem_plparser])
