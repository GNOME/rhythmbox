client_c_args = [
  '-DGNOMELOCALEDIR="' + get_option('datadir') + '/locale"',
  '-DG_LOG_DOMAIN="Rhythmbox"',
  '-DPIXMAP_DIR="' + get_option('datadir') + '/pixmaps"',
  '-DSHARE_DIR="' + get_option('datadir') + '"',
  '-DDATADIR="' + get_option('datadir') + '"',
  '-D__EXTENSIONS__',
]

executable('rhythmbox-client',
    'rb-client.c',
    c_args: client_c_args,
    dependencies: lib_dep,
    install: true,
    install_rpath: rpath
)
