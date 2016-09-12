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

shared_library('audiocd',
  audiocd_sources,
  dependencies: [rhythmbox_core_dep, totem_plparser],
  include_directories: pluginsinc,
  install_dir: audiocd_plugin_dir)

audiocd_plugin_descriptor = custom_target('audiocd-plugin-descriptor',
  input: 'audiocd.plugin.in',
  output: 'audiocd.plugin',
  command: [intltool_merge, '--quiet', '--desktop-style', meson.source_root() + '/po', '@INPUT@', '@OUTPUT@'],
  install: true,
  install_dir: audiocd_plugin_dir)

test('test-cd',
  executable('test-cd',
    'test-cd.c',
    'rb-audiocd-info.c',
    'rb-musicbrainz-lookup.c',
    dependencies: [rhythmbox_core_dep, totem_plparser]))
