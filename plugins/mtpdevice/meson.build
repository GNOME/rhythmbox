mtpdevice_plugin_dir = plugindir + '/mtpdevice'
mtpdevice_plugin_data_dir = plugindatadir + '/mtpdevice'

mtpdevice_resources = gnome.compile_resources('mtpdevice-resources',
  'mtpdevice.gresource.xml',
  source_dir: meson.current_source_dir())

mtpdevice_sources = [
  mtpdevice_resources,
  'rb-mtp-plugin.c',
  'rb-mtp-gst.c',
  'rb-mtp-gst-src.c',
  'rb-mtp-source.c',
  'rb-mtp-thread.c',
]

mtpdevice_dependencies = [rhythmbox_core_dep, libmtp]
if use_gudev
  mtpdevice_dependencies += gudev
else
  mtpdevice_dependencies += hal
endif

shared_module('mtpdevice',
  mtpdevice_sources,
  c_args: ['-DHAVE_CONFIG_H'],
  dependencies: mtpdevice_dependencies,
  install: true,
  install_dir: mtpdevice_plugin_dir)

mtpdevice_plugin_descriptor = custom_target('mtpdevice-plugin-descriptor',
  input: 'mtpdevice.plugin.desktop.in',
  output: 'mtpdevice.plugin',
  command: msgfmt_plugin_cmd,
  install: true,
  install_dir: mtpdevice_plugin_dir)
