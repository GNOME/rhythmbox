sources_headers = files(
  'rb-auto-playlist-source.h',
  'rb-browser-source.h',
  'rb-device-source.h',
  'rb-display-page-group.h',
  'rb-display-page-model.h',
  'rb-display-page-tree.h',
  'rb-display-page.h',
  'rb-media-player-source.h',
  'rb-playlist-source.h',
  'rb-playlist-xml.h',
  'rb-source-search-basic.h',
  'rb-source-search.h',
  'rb-source.h',
  'rb-static-playlist-source.h',
  'rb-streaming-source.h',
  'rb-transfer-target.h'
)

install_headers(sources_headers, subdir: 'rhythmbox/sources')

sources_sources = files(
  'rb-auto-playlist-source.c',
  'rb-browser-source.c',
  'rb-device-source.c',
  'rb-display-page-group.c',
  'rb-display-page-menu.c',
  'rb-display-page-model.c',
  'rb-display-page-tree.c',
  'rb-display-page.c',
  'rb-import-errors-source.c',
  'rb-library-source.c',
  'rb-media-player-source.c',
  'rb-missing-files-source.c',
  'rb-play-queue-source.c',
  'rb-playlist-source.c',
  'rb-source-search-basic.c',
  'rb-source-search.c',
  'rb-source.c',
  'rb-static-playlist-source.c',
  'rb-streaming-source.c',
  'rb-transfer-target.c',
  'sync/rb-sync-settings.c',
  'sync/rb-sync-settings-ui.c',
  'sync/rb-sync-state.c',
  'sync/rb-sync-state-ui.c',
)

sources_c_args = [
  '-DGNOMELOCALEDIR="' + datadir + '/locale"',
  '-DG_LOG_DOMAIN="Rhythmbox"',
  '-DPIXMAP_DIR="' + datadir + '/pixmaps"',
  '-DSHARE_DIR="' + pkgdatadir + '"',
  '-DDATADIR="' + datadir + '"',
]

if use_no_strict_aliasing
  sources_c_args += ['-fno-strict-aliasing']
endif

sources_dependencies = [totem_plparser, gstreamer_pbutils, libxml, gtk]

sources_lib = static_library('sources', sources_sources,
  c_args: sources_c_args,
  include_directories: [configinc, shellinc, sourcesinc, widgetsinc, include_directories('sync')],
  dependencies: sources_dependencies + [librb_dep, rhythmdb_dep,
    rbbackends_dep, rbmetadata_dep, rbpodcast_dep],
  install: false,
)

sources_dep = declare_dependency(
  link_with: sources_lib,
  dependencies: sources_dependencies,
  include_directories: include_directories('.'),
)
