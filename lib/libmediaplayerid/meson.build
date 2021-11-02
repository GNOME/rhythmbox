mpid_headers = files(
  'mediaplayerid.h'
)

install_headers(mpid_headers, subdir: 'rhythmbox/lib/libmediaplayerid')

mpid_sources = files(
  'mpid-device.c',
  'mpid-files.c',
  'mpid-util.c'
)

mpid_dependencies = [gio, gio_unix, glib, gobject]

# use the GUdev/media-player-id implementation if possible,
# otherwise HAL.
if use_gudev
  mpid_sources += files('mpid-udev.c')
  mpid_dependencies += [gudev]
else
  mpid_sources += files('mpid-dummy.c')
endif

mediaplayerid_lib = static_library('mediaplayerid',
  mpid_sources,
  dependencies: mpid_dependencies,
)

mediaplayerid_dep = declare_dependency(
  link_with: mediaplayerid_lib,
  dependencies: mpid_dependencies,
  include_directories: include_directories('.'),
)

mpid_gir = gnome.generate_gir(mediaplayerid_lib,
  dependencies: mpid_dependencies,
  extra_args: [
    '--library-path=@0@/../../lib/libmediaplayerid'.format(meson.current_build_dir()),
  ],
  sources: mpid_sources + mpid_headers,
  namespace: 'MPID',
  nsversion: '3.0',
  identifier_prefix: 'MPID',
  symbol_prefix: 'mpid_',
  includes: ['GObject-2.0'],
  install: true,
)
