expand_content_md_files = [
  'internals.md',
  'plugin-examples.md',
  'plugin-guide.md'
]

toml_data = configuration_data()
toml_data.set('VERSION', meson.project_version())

rb_toml = configure_file(
  input: 'rhythmbox.toml.in',
  output: 'rhythmbox.toml',
  configuration: toml_data
)

gidocgen = find_program('gi-docgen')

docs_dir = datadir / 'doc'

fs = import('fs')
fs.copyfile(meson.project_source_root() / 'data/icons/hicolor/scalable/apps/org.gnome.Rhythmbox3.svg')

custom_target('apidoc',
  input: [ rb_toml, rb_gir ],
  output: 'rhythmbox',
  command: [
    gidocgen,
    'generate',
    '--quiet',
    '--add-include-path=@0@'.format(meson.current_build_dir() / '../lib/libmediaplayerid'),
    '--config=@INPUT0@',
    '--output-dir=@OUTPUT@',
    '--no-namespace-dir',
    '--content-dir=@0@'.format(meson.current_source_dir()),
    '@INPUT1@',
  ],
  depend_files: [ expand_content_md_files ],
  build_by_default: true,
  install: true,
  install_dir: docs_dir
)
