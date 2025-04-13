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
]

rhythmbox_core_include_directories = [
  configinc, libinc, mpidinc, metadatainc, rhythmdbinc, backendsinc, pluginsinc,
  podcastinc, sourcesinc, widgetsinc, include_directories('.'),
]

rhythmbox_core_deps = [
  common_deps, mediaplayerid_dep, librb_dep, rhythmdb_dep, rbbackends_dep, rbmetadata_dep,
  rbpodcast_dep, rbwidgets_dep, sources_dep, libmtp
]

if enable_python
  rhythmbox_core_deps += [pygobject]
  if enable_gir2
    rhythmbox_core_deps += [girepository]
  else
    rhythmbox_core_deps += [gobject_introspection]
  endif
endif

librhythmbox_core = shared_library('rhythmbox-core',
  shell_sources,
  c_args: shell_c_args,
  include_directories: rhythmbox_core_include_directories,
  dependencies: rhythmbox_core_deps,
  install: true,
  link_whole: [librb, rbwidgets_lib, sources_lib],
  version: '@0@.@1@.@2@'.format(RHYTHMBOX_CORE_CURRENT, RHYTHMBOX_CORE_REVISION, RHYTHMBOX_CORE_AGE)
)

rhythmbox_core_dep = declare_dependency(
  link_with: librhythmbox_core,
  dependencies: common_deps,
  include_directories: rhythmbox_core_include_directories,
)

rb_gir = gnome.generate_gir(librhythmbox_core,
  dependencies: common_deps,
  link_with: [],
  extra_args: [ '--accept-unprefixed'],
  sources: lib_headers + lib_sources + 
    backends_sources + backends_headers +
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

# set rpath on the rhythmbox binary if we're installing inside
# the source dir, ensuring it loads librhythmbox-core from this
# build rather than a package installed on the system.
# if installing elsewhere, getting the library search path right
# is the user's responsibility.
rpath = ''

# try to use realpath(1) to resolve symlinks in the prefix path,
# since meson.project_source_root() is a resolved path.
real_prefix = get_option('prefix')
realpath = find_program('realpath', required: false)
if realpath.found()
  realpath_result = run_command([realpath, real_prefix], check: false)
  if realpath_result.returncode() == 0
    real_prefix = realpath_result.stdout().strip()
  endif
endif

if real_prefix.startswith(meson.project_source_root())
  rpath = real_prefix / get_option('libdir')
endif

executable('rhythmbox', 'main.c',
  dependencies: [ rhythmbox_core_dep ],
  c_args: shell_c_args,
  include_directories: [ pluginsinc ],
  install: true,

  install_rpath: rpath
)
