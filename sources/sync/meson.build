sourcesync_sources = files(
  'rb-sync-settings.c',
  'rb-sync-settings-ui.c',
  'rb-sync-state.c',
  'rb-sync-state-ui.c',
)

sourcesync_c_args = [
  '-DGNOMELOCALEDIR="' + get_option('datadir') + '/locale"',
  '-DG_LOG_DOMAIN="Rhythmbox"',
  '-DPIXMAP_DIR="' + get_option('datadir') + '/pixmaps"',
  '-DSHARE_DIR="' + get_option('datadir') + '"',
  '-DDATADIR="' + get_option('datadir') + '"',
]

if use_no_strict_aliasing
  sourcesync_c_args += ['-fno-strict-aliasing']
endif

sourcesync_link_args = ['-export-dynamic']
