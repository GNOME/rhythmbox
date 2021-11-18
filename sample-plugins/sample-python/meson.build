sample_python_plugin_dir = sampleplugindir + '/sample-python'
sample_python_plugin_data_dir = sampleplugindatadir + '/sample-python'

sample_python_plugin_files = [
  'sample-python.py',
]

install_data(sample_python_plugin_files,
  install_dir: sample_python_plugin_dir)

sample_python_plugin_descriptor = custom_target('sample-python-plugin-descriptor',
  input: 'sample-python.plugin.desktop.in',
  output: 'sample-python.plugin',
  command: msgfmt_plugin_cmd,
  install: true,
  install_dir: sample_python_plugin_dir)
