metadata_headers = files(
  'rb-ext-db-key.h',
  'rb-ext-db.h',
  'rb-metadata.h'
)

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
  'rb-metadata-dbus-service.c',
  'rb-metadata-gst.c',
  'rb-metadata-gst-common.c'
]

full_libexecdir = get_option('prefix') / get_option('libexecdir')
metadata_c_args = [
  '-DGNOMELOCALEDIR="' + datadir + '/locale"',
  '-DG_LOG_DOMAIN="Rhythmbox"',
  '-DLIBEXEC_DIR="@0@"'.format(full_libexecdir),
  # In configure.ac this variable was theoretically customizable by passing
  # --program-suffix when running 'configure', but it's not clear how
  # that would actually work or whether anyone used it.
  '-DINSTALLED_METADATA_HELPER="rhythmbox-metadata"'
]

if use_no_strict_aliasing
  metadata_c_args += ['-fno-strict-aliasing']
endif

# client library

rbmetadata_lib = static_library('rbmetadata', metadata_sources,
  c_args: metadata_c_args,
  dependencies: [common_deps, librb_dep],
  install: false,
)

rbmetadata_dep = declare_dependency(
  link_with: rbmetadata_lib,
  dependencies: common_deps,
  include_directories: include_directories('.'),
)

# service

executable('rhythmbox-metadata', metadata_service_sources,
  c_args: metadata_c_args,
  link_with: rbmetadata_lib,
  dependencies: [librb_dep, rbmetadata_dep, intl],
  install: true,
  install_dir: get_option('libexecdir')
)

# test program

executable('test-metadata', 'test-metadata.c',
  link_with: rbmetadata_lib,
  dependencies: [librb_dep, rbmetadata_dep],
)
