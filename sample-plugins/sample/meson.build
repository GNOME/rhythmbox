sample_plugin_dir = sampleplugindir + '/sample'

sample_sources = [
  'rb-sample-plugin.c'
]

shared_module('sample',
  sample_sources,
  c_args: ['-DHAVE_CONFIG_H'],
  dependencies: [rhythmbox_core_dep],
  install_dir: sample_plugin_dir,
  install: true)

sample_plugin_descriptor = custom_target('sample-plugin-descriptor',
  input: 'sample.plugin.in',
  output: 'sample.plugin',
  command: [intltool_merge, '--quiet', '--desktop-style', meson.project_source_root() + '/po', '@INPUT@', '@OUTPUT@'],
  install: true,
  install_dir: sample_plugin_dir)
