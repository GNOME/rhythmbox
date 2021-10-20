brasero_disc_recorder_plugin_dir = plugindir + '/cd-recorder'
brasero_disc_recorder_plugin_data_dir = plugindatadir + '/cd-recorder'

brasero_disc_recorder_sources = [
  'rb-disc-recorder-plugin.c'
]

shared_module('cd-recorder',
  brasero_disc_recorder_sources,
  c_args: ['-DHAVE_CONFIG_H'],
  dependencies: [rhythmbox_core_dep, brasero_media],
  install: true,
  install_dir: brasero_disc_recorder_plugin_dir)

brasero_disc_recorder_plugin_descriptor = custom_target('brasero-disc-recorder-plugin-descriptor',
  input: 'cd-recorder.desktop.plugin.in',
  output: 'cd-recorder.plugin',
  command: msgfmt_plugin_cmd,
  install: true,
  install_dir: brasero_disc_recorder_plugin_dir)
