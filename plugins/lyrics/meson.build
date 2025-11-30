lyrics_plugin_dir = plugindir + '/lyrics'
lyrics_plugin_data_dir = plugindatadir + '/lyrics'

lyrics_plugin_files = [
  'LyricsParse.py',
  'LyricsSites.py',
  'LyricsConfigureDialog.py',
  'lyrics.py',
  'TerraParser.py',
  'DarkLyricsParser.py',
  'JlyricParser.py']

install_data(lyrics_plugin_files,
  install_dir: lyrics_plugin_dir)

lyrics_plugin_descriptor = custom_target('lyrics-plugin-descriptor',
  input: 'lyrics.plugin.desktop.in',
  output: 'lyrics.plugin',
  command: msgfmt_plugin_cmd,
  install: true,
  install_dir: lyrics_plugin_dir)

install_data('lyrics-prefs.ui',
  install_dir: lyrics_plugin_data_dir)
