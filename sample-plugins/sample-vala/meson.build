sample_vala_plugin_dir = sampleplugindir + '/sample-vala'

sample_vala_sources = [
  'rb-sample-vala-plugin.vala'
]

vala_deps = [
  dependency('glib-2.0'),
  dependency('gobject-2.0'),
  meson.get_compiler('vala').find_library('rhythmdb', dirs: vapi_dir),
  meson.get_compiler('vala').find_library('rb', dirs: vapi_dir),
  dependency('gtk+-3.0'),
  dependency('gstreamer-1.0'),
  dependency('libpeas-1.0')
]

shared_module('sample-vala',
  sample_vala_sources,
  dependencies: vala_deps,
  install_dir: sample_vala_plugin_dir,
  install: true)

sample_vala_plugin_descriptor = custom_target('sample-vala-plugin-descriptor',
  input: 'sample-vala.plugin.desktop.in',
  output: 'sample-vala.plugin',
  command: msgfmt_plugin_cmd,
  install: true,
  install_dir: sample_vala_plugin_dir)
