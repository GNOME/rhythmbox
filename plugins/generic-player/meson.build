generic_player_plugin_dir = plugindir + '/generic-player'

generic_player_resources = gnome.compile_resources('generic-player-resources',
  'generic-player.gresource.xml',
  source_dir: meson.current_source_dir())

generic_player_sources = [
  generic_player_resources,
  'rb-generic-player-plugin.c',
  'rb-generic-player-source.c',
  'rb-generic-player-playlist-source.c',
  'rb-nokia770-source.c',
  'rb-psp-source.c',
]

shared_module('generic-player',
  generic_player_sources,
  dependencies: [rhythmbox_core_dep],
  install: true,
  install_dir: generic_player_plugin_dir)

generic_player_plugin_descriptor = custom_target('generic-player-plugin-descriptor',
  input: 'generic-player.plugin.desktop.in',
  output: 'generic-player.plugin',
  command: msgfmt_plugin_cmd,
  install: true,
  install_dir: generic_player_plugin_dir)
