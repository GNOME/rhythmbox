podcast_headers = [
  'rb-podcast-entry-types.h',
  'rb-podcast-parse.h',
  'rb-podcast-manager.h',
  'rb-podcast-search.h'
]

install_headers(podcast_headers, subdir: 'rhythmbox/podcast')

podcast_parse_sources = [
  'rb-podcast-parse.c',
  'rb-podcast-search.c',
  'rb-podcast-search-itunes.c',
  'rb-podcast-search-miroguide.c'
]

podcast_sources = files(
  'rb-feed-podcast-properties-dialog.c',
  'rb-podcast-add-dialog.c',
  'rb-podcast-properties-dialog.c',
  'rb-podcast-main-source.c',
  'rb-podcast-search.c',
  'rb-podcast-search-itunes.c',
  'rb-podcast-search-miroguide.c',
  'rb-podcast-source.c',
  'rb-podcast-parse.c',
  'rb-podcast-manager.c',
  'rb-podcast-entry-types.c'
)

podcast_c_args = [
  '-DGNOMELOCALEDIR="' + get_option('datadir') + '/locale"',
  '-DG_LOG_DOMAIN="RhythmBox"'
]

podcast_dependencies = [ json_glib, libsoup, totem_plparser ]

##

librbpodcast_parse = static_library('rbpodcast_parse', podcast_parse_sources,
  c_args: c_args,
  dependencies: [lib_dep, json_glib, libsoup, totem_plparser])

test_podcast_search = executable('test-podcast-search',
  'test-podcast-search.c',
  c_args: c_args,
  include_directories: [configinc, libinc],
  dependencies: [lib_dep, json_glib, totem_plparser],
  link_with: [librbpodcast_parse])

test_podcast_parse = executable('test-podcast-parse',
  'test-podcast-parse.c',
  c_args: c_args,
  dependencies: [lib_dep, totem_plparser],
  link_with: [librbpodcast_parse])

if enable_browser_plugin
  mozilla_plugin = shared_library(
    'rhythmbox-itms-plugin.c',
    install_dir: mozilla_plugindir)
endif
