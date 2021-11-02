rb_plugin_dir = plugindir + '/rb'

rbconfig_py = configure_file(
  input: 'rbconfig.py.in',
  output: 'rbconfig.py',
  install_dir: rb_plugin_dir,
  configuration: cdata)

rb_plugin_files = [
  'Loader.py',
  'Coroutine.py',
  'URLCache.py',
  'stringmatch.py',
  'rb.py',
  'rb.plugin']

install_data(rb_plugin_files,
  install_dir: rb_plugin_dir)
