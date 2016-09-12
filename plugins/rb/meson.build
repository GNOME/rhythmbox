rb_plugin_dir = plugindir + '/rb'

configure_file(
  input: 'rbconfig.py.in',
  output: 'rbconfig.py',
  configuration: cdata)

rb_plugin_files = [
  'Loader.py',
  'Coroutine.py',
  'URLCache.py',
  'stringmatch.py',
  'rbconfig.py',
  'rb.py',
  'rb.plugin']

install_data(rb_plugin_files,
  install_dir: rb_plugin_dir)
