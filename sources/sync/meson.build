sourcesync_sources = files(
  'rb-sync-settings.c',
  'rb-sync-settings-ui.c',
  'rb-sync-state.c',
  'rb-sync-state-ui.c',
)

sourcesync_c_args = [
  '-DGNOMELOCALEDIR="' + datadir + '/locale"',
  '-DG_LOG_DOMAIN="Rhythmbox"',
  '-DPIXMAP_DIR="' + datadir + '/pixmaps"',
  '-DSHARE_DIR="' + pkgdatadir + '"',
  '-DDATADIR="' + datadir + '"',
]

if use_no_strict_aliasing
  sourcesync_c_args += ['-fno-strict-aliasing']
endif

sourcesync_dependencies = [librb_dep, rbpodcast_dep, rhythmdb_dep]

sourcesync_lib = static_library('sourcesync', sourcesync_sources,
  c_args: sourcesync_c_args,
  include_directories: [configinc, shellinc, sourcesinc, widgetsinc],
  dependencies: sourcesync_dependencies,
  install: false,
)

sourcesync_dep = declare_dependency(
  link_with: sourcesync_lib,
  dependencies: sourcesync_dependencies,
  include_directories: [include_directories('.')],
)
