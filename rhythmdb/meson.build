rhythmdb_headers = files(
  'rb-refstring.h',
  'rhythmdb.h',
  'rhythmdb-entry.h',
  'rhythmdb-entry-type.h',
  'rhythmdb-import-job.h',
  'rhythmdb-property-model.h',
  'rhythmdb-query-model.h',
  'rhythmdb-query-result-list.h',
  'rhythmdb-query-results.h',
)

install_headers(rhythmdb_headers, subdir: 'rhythmbox/rhythmdb')

rhythmdb_sources = files(
  'rb-refstring.c',
  'rhythmdb.c',
  'rhythmdb-dbus.c',
  'rhythmdb-entry-type.c',
  'rhythmdb-import-job.c',
  'rhythmdb-metadata-cache.c',
  'rhythmdb-monitor.c',
  'rhythmdb-property-model.c',
  'rhythmdb-query-model.c',
  'rhythmdb-query-result-list.c',
  'rhythmdb-query-results.c',
  'rhythmdb-query.c',
  'rhythmdb-song-entry-types.c',
  'rhythmdb-tree.c',
)

c_args = [
  '-DGNOMELOCALEDIR="' + datadir + '/locale"',
  '-DG_LOG_DOMAIN="RhythmDB"',
  '-DSHARE_DIR="' + pkgdatadir + '"',
  '-DDATADIR="' + datadir + '"'
]

if use_no_strict_aliasing
  c_args += ['-fno-strict-aliasing']
endif

librhythmdb = static_library('rhythmdb', rhythmdb_sources,
  c_args: c_args,
  dependencies: [common_deps, librb_dep],
  include_directories: [podcastinc, widgetsinc])

rhythmdb_dep = declare_dependency(
  link_with: librhythmdb,
  include_directories: include_directories('.'),
  dependencies: common_deps,
)
