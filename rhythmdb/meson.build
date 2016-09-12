headers = [
  'rb-refstring.h',
  'rhythmdb.h',
  'rhythmdb-property-model.h',
  'rhythmdb-query-model.h',
  'rhythmdb-query-result-list.h',
  'rhythmdb-query-results.h',
  'rhythmdb-import-job.h',
  'rhythmdb-entry.h',
  'rhythmdb-entry-type.h',
  'rhythmdb-metadata-cache.h'
]

install_headers(headers, subdir: 'rhythmbox/rhythmdb')

sources = [
  'rb-refstring.c',
  'rhythmdb.c',
  'rhythmdb-monitor.c',
  'rhythmdb-query.c',
  'rhythmdb-property-model.c',
  'rhythmdb-query-model.c',
  'rhythmdb-query-result-list.c',
  'rhythmdb-query-results.c',
  'rhythmdb-import-job.c',
  'rhythmdb-entry-type.c',
  'rhythmdb-song-entry-types.c',
  'rhythmdb-dbus.c',
  'rhythmdb-metadata-cache.c'
]

if get_option('database') == 'tree'
  sources += ['rhythmdb-tree.c']
endif

c_args = [
  '-DGNOMELOCALEDIR="' + get_option('datadir') + '/locale"',
  '-DG_LOG_DOMAIN="RhythmDB"',
  '-DSHARE_DIR="' + get_option('datadir') + '"',
  '-DDATADIR="' + get_option('datadir') + '"'
]

if use_no_strict_aliasing
  c_args += ['-fno-strict-aliasing']
endif

rhythmdb_dependencies = [lib_dep, libmath]

librhythmdb = static_library('rhythmdb', sources,
  c_args: c_args,
  dependencies: rhythmdb_dependencies,
  include_directories: [podcastinc, widgetsinc],
  link_args: '-export-dynamic')

rhythmdb_dep = declare_dependency(
  link_with: librhythmdb,
  include_directories: include_directories('.'),
  dependencies: rhythmdb_dependencies,
)
