gnome = import('gnome')

make_tab_command = ['sed', '-e', 's/^/"/', '-e', 's/$/",/', '@INPUT@']

authors_tab = custom_target('AUTHORS.tab',
  input: '../AUTHORS', output: 'AUTHORS.tab',
  command: make_tab_command, capture: true
)

maintainers_tab = custom_target('MAINTAINERS.tab',
  input: '../MAINTAINERS', output: 'MAINTAINERS.tab',
  command: make_tab_command, capture: true
)

maintainers_old_tab = custom_target('MAINTAINERS.old.tab',
  input: '../MAINTAINERS.old', output: 'MAINTAINERS.old.tab',
  command: make_tab_command, capture: true
)

documenters_tab = custom_target('DOCUMENTERS.tab',
  input: '../DOCUMENTERS', output: 'DOCUMENTERS.tab',
  command: make_tab_command, capture: true
)

shell_headers = [
  'rb-shell.h',
  'rb-shell-player.h',
  'rb-shell-preferences.h',
  'rb-playlist-manager.h',
  'rb-removable-media-manager.h',
  'rb-history.h',
  'rb-play-order.h',
  'rb-track-transfer-batch.h',
  'rb-track-transfer-queue.h'
]

install_headers(shell_headers, subdir: 'rhythmbox/shell')

resources = gnome.compile_resources('rb-resources', 'rhythmbox.gresource.xml',
  source_dir: ['../data/'],
  dependencies: playlists_xml)

shell_sources = [
  authors_tab,
  documenters_tab,
  maintainers_tab,
  maintainers_old_tab,
  'rb-application.c',
  'rb-history.c',
  'rb-play-order.c',
  'rb-play-order-linear.c',
  'rb-play-order-linear-loop.c',
  'rb-play-order-queue.c',
  'rb-play-order-random.c',
  'rb-play-order-random-by-age.c',
  'rb-play-order-random-by-age-and-rating.c',
  'rb-play-order-random-by-rating.c',
  'rb-play-order-random-equal-weights.c',
  'rb-play-order-shuffle.c',
  'rb-playlist-manager.c',
  'rb-removable-media-manager.c',
  'rb-shell.c',
  'rb-shell-clipboard.c',
  'rb-shell-player.c',
  'rb-shell-preferences.c',
  'rb-task-list.c',
  'rb-track-transfer-batch.c',
  'rb-track-transfer-queue.c',
  resources
]

shell_c_args = [
  '-DGNOMELOCALEDIR="' + get_option('datadir') + '/locale"',
  '-DG_LOG_DOMAIN="Rhythmbox"',
  '-DPIXMAP_DIR="' + get_option('datadir') + '/pixmaps"',
  '-DSHARE_DIR="' + get_option('datadir') + '"',
  '-DDATADIR="' + get_option('datadir') + '"',
  '-DLIBDIR="' + get_option('libdir') + '"',
  '-D__EXTENSIONS__',
]

shell_dependencies = []

rhythmbox_core_dependencies = [ lib_dep, rhythmdb_dep, gstreamer_plugins_base, gstreamer_base, gstreamer_tag, gstreamer_pbutils, gstreamer_audio, gstreamer_controller, gudev ] + (
  backend_dependencies + gstreamer_backend_dependencies + metadata_dependencies + mpid_dependencies +
  podcast_dependencies + shell_dependencies + source_dependencies + widget_dependencies)

rhythmbox_core_include_directories = [
  configinc, backendsinc, gstreamerbackendinc, libinc, libmediaplayeridinc, metadatainc,
  podcastinc, pluginsinc, rhythmdbinc, shellinc, sourcesinc, sourcessyncinc,
  widgetsinc
]

librhythmbox_core = shared_library('rhythmbox_core',
  backend_sources + gstreamer_backend_sources + metadata_sources + mpid_sources,
  podcast_sources + shell_sources + source_sources + sourcesync_sources +
  widget_sources,
  c_args: shell_c_args,
  include_directories: rhythmbox_core_include_directories,
  dependencies: rhythmbox_core_dependencies,
  install: 'true',
  link_args: ['-export-dynamic'],
  version: '@0@.@1@.@2@'.format(RHYTHMBOX_CORE_CURRENT, RHYTHMBOX_CORE_REVISION, RHYTHMBOX_CORE_AGE)
)

rhythmbox_core_dep = declare_dependency(
  link_with: librhythmbox_core,
  dependencies: rhythmbox_core_dependencies,
  include_directories: rhythmbox_core_include_directories,
)

deps_rhythmbox = [gobject_introspection,
                  gstreamer_base, gstreamer_plugins_base,
                  gtk, gdk_pixbuf, gio, gio_unix, libsoup, 
                  tdb, json_glib]

if gdk_targets.contains('x11')
  deps_rhythmbox = deps_rhythmbox + [x11]
endif

if enable_python
  deps_rhythmbox += [pygobject]
endif

executable('rhythmbox', 'main.c',
  dependencies: deps_rhythmbox,
  include_directories: [
    configinc, backendsinc, libinc, libmediaplayeridinc, metadatainc,
    podcastinc, pluginsinc, rhythmdbinc, shellinc, sourcesinc
  ],
  link_args: ['-export-dynamic', '-Wno-undef'],
  link_with: librhythmbox_core,
  install: true,
  install_rpath: rpath
)
