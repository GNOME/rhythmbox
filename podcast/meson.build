podcast_headers = files(
  'rb-podcast-parse.h',
  'rb-podcast-manager.h',
  'rb-podcast-search.h'
)

install_headers(podcast_headers, subdir: 'rhythmbox/podcast')

podcast_parse_sources = files(
  'rb-podcast-parse.c',
  'rb-podcast-search.c',
  'rb-podcast-search-itunes.c',
)

podcast_sources = files(
  'rb-feed-podcast-properties-dialog.c',
  'rb-podcast-add-dialog.c',
  'rb-podcast-properties-dialog.c',
  'rb-podcast-main-source.c',
  'rb-podcast-search.c',
  'rb-podcast-search-itunes.c',
  'rb-podcast-source.c',
  'rb-podcast-parse.c',
  'rb-podcast-manager.c',
  'rb-podcast-entry-types.c'
)

podcast_c_args = [
  '-DGNOMELOCALEDIR="' + datadir + '/locale"',
  '-DG_LOG_DOMAIN="RhythmBox"'
]

rbpodcast_lib = static_library('rbpodcast', podcast_sources,
  c_args: lib_c_args,
  include_directories: [configinc, shellinc, sourcesinc, widgetsinc],
  dependencies: [common_deps, librb_dep, rhythmdb_dep, rbmetadata_dep],
  install: false,
)

rbpodcast_dep = declare_dependency(
  link_with: rbpodcast_lib,
  dependencies: common_deps,
  include_directories: [configinc, include_directories('.')],
)

##

librbpodcast_parse = static_library('rbpodcast_parse', podcast_parse_sources,
  c_args: c_args,
  dependencies: [librb_dep, json_glib, libsoup, totem_plparser])

test_podcast_search = executable('test-podcast-search',
  'test-podcast-search.c',
  c_args: c_args,
  dependencies: [librb_dep, json_glib, totem_plparser],
  link_with: [librbpodcast_parse])

test_podcast_parse = executable('test-podcast-parse',
  'test-podcast-parse.c',
  c_args: c_args,
  dependencies: [librb_dep, totem_plparser],
  link_with: [librbpodcast_parse])
