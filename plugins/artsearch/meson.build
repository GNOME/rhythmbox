artsearch_plugin_dir = plugindir + '/artsearch'

artsearch_plugin_files = [
  'artsearch.py',
  'songinfo.py',
  'embedded.py',
  'lastfm.py',
  'local.py',
  'musicbrainz.py',
  'oldcache.py']

install_data(artsearch_plugin_files,
  install_dir: artsearch_plugin_dir)

artsearch_plugin_descriptor = custom_target('artsearch-plugin-descriptor',
  input: 'artsearch.plugin.desktop.in',
  output: 'artsearch.plugin',
  command: msgfmt_plugin_cmd,
  install: true,
  install_dir: artsearch_plugin_dir)
