metadata_headers = [
  'rb-ext-db-key.h',
  'rb-ext-db.h',
  'rb-metadata.h'
]

install_headers(metadata_headers, subdir: 'rhythmbox/metadata')

metadata_sources = files(
  'rb-ext-db-key.c',
  'rb-ext-db.c',
  'rb-metadata-common.c',
  'rb-metadata-dbus.c',
  'rb-metadata-dbus-client.c'
)

metadata_service_sources = [
  'rb-metadata-common.c',
  'rb-metadata-dbus.c',
  'rb-metadata-gst.c',
  'rb-metadata-gst-common.c'
]

metadata_dbus_service_sources = [
  'rb-metadata-dbus-service.c'
]

metadata_c_args = [
  '-DGNOMELOCALEDIR="' + get_option('datadir') + '/locale"',
  '-DG_LOG_DOMAIN="Rhythmbox"',
  '-DLIBEXECDIR="' + get_option('libexecdir') + '"',
  # In configure.ac this variable was theoretically customizable by passing
  # --program-suffix when running 'configure', but it's not clear how
  # that would actually work or whether anyone used it.
  '-DINSTALLED_METADATA_HELPER="rhythmbox-metadata"'
]

if use_no_strict_aliasing
  metadata_c_args += ['-fno-strict-aliasing']
endif

# client library

metadata_dependencies = [lib_dep, tdb]
metadata_link_args = ['-export-dynamic']

# (this gets linked into librhythmbox-core)

# service

librbmetadatasvc = static_library('rbmetadatasvc', metadata_service_sources,
  dependencies: [lib_dep, glib, gobject, gstreamer, gstreamer_pbutils, gtk])

executable('rhythmbox-metadata', metadata_dbus_service_sources,
  link_with: librbmetadatasvc,
  dependencies: [glib, gobject, gstreamer, gstreamer_pbutils, gtk],
  include_directories: [configinc, libinc],
  install: true,
  install_dir: get_option('libexecdir'),
  install_rpath: rpath
)
