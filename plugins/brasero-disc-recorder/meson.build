brasero_disc_recorder_plugin_dir = plugindir + '/brasero-disc-recorder'
brasero_disc_recorder_plugin_data_dir = plugindatadir + '/brasero-disc-recorder'

brasero_disc_recorder_sources = [
  'rb-disc-recorder-plugin.c'
]

shared_library('cd-recorder',
  brasero_disc_recorder_sources,
  c_args: ['-DHAVE_CONFIG_H'],
  dependencies: [rhythmbox_core_dep, brasero_media],
  install_dir: brasero_disc_recorder_plugin_dir)

brasero_disc_recorder_plugin_descriptor = custom_target('brasero-disc-recorder-plugin-descriptor',
  input: 'cd-recorder.plugin.in',
  output: 'cd-recorder.plugin',
  command: [intltool_merge, '--quiet', '--desktop-style', meson.source_root() + '/po', '@INPUT@', '@OUTPUT@'],
  install: true,
  install_dir: brasero_disc_recorder_plugin_dir)
