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

shell_headers = files(
  'rb-application.h',
  'rb-shell.h',
  'rb-shell-player.h',
  'rb-shell-preferences.h',
  'rb-playlist-manager.h',
  'rb-removable-media-manager.h',
  'rb-history.h',
  'rb-play-order.h',
  'rb-task-list.h',
  'rb-track-transfer-batch.h',
  'rb-track-transfer-queue.h'
)

install_headers(shell_headers, subdir: 'rhythmbox/shell')

resources = gnome.compile_resources('rb-resources', 'rhythmbox.gresource.xml',
  source_dir: ['../data/'],
  dependencies: playlists_xml)

shell_sources = files(
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
) + [
  resources,
  authors_tab,
  documenters_tab,
  maintainers_tab,
  maintainers_old_tab,
]

shell_c_args = [
  '-DGNOMELOCALEDIR="' + datadir + '/locale"',
  '-DG_LOG_DOMAIN="Rhythmbox"',
  '-DPIXMAP_DIR="' + datadir + '/pixmaps"',
  '-DSHARE_DIR="' + pkgdatadir + '"',
  '-DDATADIR="' + datadir + '"',
  '-DLIBDIR="' + libdir + '"',
  '-D__EXTENSIONS__',
]

rhythmbox_core_dependencies = [gobject_introspection, libpeas]

rhythmbox_core_include_directories = [
  configinc, libinc, mpidinc, metadatainc, rhythmdbinc, backendsinc, pluginsinc,
  podcastinc, sourcesinc, sourcessyncinc, widgetsinc, include_directories('.'),
]

librhythmbox_core = shared_library('rhythmbox-core',
  shell_sources,
  c_args: shell_c_args,
  include_directories: rhythmbox_core_include_directories,
  dependencies: [rhythmbox_core_dependencies,  mediaplayerid_dep, rhythmdb_dep,
    rbbackends_dep, rbpodcast_dep, rbwidgets_dep, sources_dep],
  install: 'true',
  link_whole: [librb, rbwidgets_lib, sources_lib],
  version: '@0@.@1@.@2@'.format(RHYTHMBOX_CORE_CURRENT, RHYTHMBOX_CORE_REVISION, RHYTHMBOX_CORE_AGE)
)

# We can't use the _dep objects from the helper libraries here
# as we need to avoid linking the static helper libraries to the
# plugins.
rhythmbox_core_dep = declare_dependency(
  link_with: librhythmbox_core,
  dependencies: [mpid_dependencies, lib_dependencies, rhythmdb_dependencies,
    gstreamer_backend_dependencies, backends_dependencies,
    podcast_dependencies, widgets_dependencies, sources_dependencies],
  include_directories: rhythmbox_core_include_directories,
)

rb_gir = gnome.generate_gir(librhythmbox_core,
  dependencies: [rhythmbox_core_dependencies],
  link_with: [],
  extra_args: [ '--accept-unprefixed'],
  sources: lib_headers + lib_sources + 
    backends_sources + backends_headers +
    gstreamer_backend_sources + gstreamer_backend_headers +
    metadata_sources + metadata_headers +
    podcast_sources + podcast_headers +
    rhythmdb_sources + rhythmdb_headers +
    shell_sources + shell_headers +
    sources_sources + sources_headers +
    widgets_sources + widgets_headers,
  namespace: 'RB',
  nsversion: '3.0',
  identifier_prefix: 'RB',
  symbol_prefix: 'rb_',
  includes: ['GObject-2.0', 'Gio-2.0', 'Gtk-3.0', 'Gst-1.0',
    'GstPbutils-1.0', 'libxml2-2.0', mpid_gir[0]],
  install: true,
)

rhythmbox_dependencies = [rhythmbox_core_dep, gobject_introspection,
  gio, gio_unix, libsoup, tdb, json_glib]

if gdk_targets.contains('x11')
  rhythmbox_dependencies += [x11]
endif

if enable_python
  rhythmbox_dependencies += [pygobject]
endif

executable('rhythmbox', 'main.c',
  dependencies: rhythmbox_dependencies,
  include_directories: [ pluginsinc ],
  install: true,
  install_rpath: rpath
)
