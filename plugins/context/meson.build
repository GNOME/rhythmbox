context_plugin_dir = plugindir + '/context'
context_plugin_data_dir = plugindatadir + '/context'

context_plugin_files = [
  'AlbumTab.py',
  'ArtistTab.py',
  'ContextView.py',
  'LastFM.py',
  'LinksTab.py',
  'LyricsTab.py',
  'context.py'
]

install_data(context_plugin_files,
  install_dir: context_plugin_dir)

context_plugin_descriptor = custom_target('context-plugin-descriptor',
  input: 'context.plugin.desktop.in',
  output: 'context.plugin',
  command: msgfmt_plugin_cmd,
  install: true,
  install_dir: context_plugin_dir)

install_data([
  'tmpl/album-tmpl.html',
  'tmpl/artist-tmpl.html',
  'tmpl/loading.html',
  'tmpl/links-tmpl.html',
  'tmpl/lyrics-tmpl.html',
  'tmpl/main.css'],
  install_dir: context_plugin_data_dir / 'tmpl')

install_data([
  'img/lastfm.png',
  'img/spinner.gif',
  'img/links/Allmusic16x16.png',
  'img/links/Discogs16x16.png',
  'img/links/Wikipedia16x16.png'],
  install_dir: context_plugin_data_dir / 'img')
