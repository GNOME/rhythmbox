gir_dir = join_paths(datadir, 'gir-1.0')
typelib_dir = join_paths(libdir, 'girepository-1.0')

mpid_introspection_sources = [
  mpid_headers,
  mpid_sources,
]

mpid_introspection_incs = [
  'GObject-2.0'
]


mpid_gir_target = gnome.generate_gir(
  librhythmbox_core,
  sources: mpid_introspection_sources,
  nsversion: '3.0',
  namespace: 'MPID',
  symbol_prefix: 'mpid_',
  identifier_prefix: 'MPID',
  includes: mpid_introspection_incs,
  include_directories: include_directories('../../'),
  install: true,
  install_dir_gir: gir_dir,
  install_dir_typelib: typelib_dir,
)
mpid_gir = mpid_gir_target[0]

rb_introspection_sources = [
  backends_headers,
  backends_sources,
  gstreamer_backend_headers,
  gstreamer_backend_sources,
  lib_headers,
  lib_sources,
  metadata_headers,
  metadata_sources,
  podcast_headers,
  podcast_sources,
  rhythmdb_headers,
  rhythmdb_sources,
  shell_headers,
  shell_sources,
  sources_headers,
  sources_sources,
  widgets_headers,
  widgets_sources,
]

rb_introspection_incs = [
  'GObject-2.0',
  'Gio-2.0',
  'Gtk-3.0',
  'Gst-1.0',
  'GstPbutils-1.0',
  'libxml2-2.0',
  mpid_gir
]

rb_gir = gnome.generate_gir(
  librhythmbox_core,
  sources: rb_introspection_sources,
  nsversion: '3.0',
  namespace: 'RB',
  symbol_prefix: 'rb_',
  identifier_prefix: 'RB',
  includes: rb_introspection_incs,
  include_directories: include_directories('../../'),
  install: true,
  install_dir_gir: gir_dir,
  install_dir_typelib: typelib_dir,
  extra_args: [ '--accept-unprefixed' ],
)
