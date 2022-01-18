sample_plugin_dir = sampleplugindir + '/sample'

sample_sources = [
  'rb-sample-plugin.c'
]

shared_module('sample',
  sample_sources,
  dependencies: [rhythmbox_core_dep],
  install_dir: sample_plugin_dir,
  install: true)

sample_plugin_descriptor = custom_target('sample-plugin-descriptor',
  input: 'sample.plugin.desktop.in',
  output: 'sample.plugin',
  command: msgfmt_plugin_cmd,
  install: true,
  install_dir: sample_plugin_dir)
