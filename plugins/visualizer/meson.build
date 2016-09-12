visualizer_plugin_dir = plugindir + '/visualizer'
visualizer_plugin_data_dir = plugindir + '/visualizer'

visualizer_sources = [
  'rb-visualizer-menu.c',
  'rb-visualizer-page.c',
  'rb-visualizer-fullscreen.c',
  'rb-visualizer-plugin.c']

shared_library('visualizer',
  visualizer_sources,
  c_args: [
    '-DPLUGIN_SRC_DIR="@0@"'.format(meson.current_source_dir())
  ],
  dependencies: [rhythmbox_core_dep, clutter, clutter_gst, clutter_gtk, mx],
  include_directories: pluginsinc,
  install_dir: visualizer_plugin_dir)

visualizer_plugin_descriptor = custom_target('visualizer-plugin-descriptor',
  input: 'visualizer.plugin.in',
  output: 'visualizer.plugin',
  command: [intltool_merge, '--quiet', '--desktop-style', meson.source_root() + '/po', '@INPUT@', '@OUTPUT@'],
  install: true,
  install_dir: visualizer_plugin_dir)

install_data([
  'visualizer.css',
  'button-active.png',
  'button-disabled.png',
  'button-focus.png',
  'button-hover.png',
  'button.png',
  'visualizer-box.png'],
  install_dir: visualizer_plugin_data_dir)
