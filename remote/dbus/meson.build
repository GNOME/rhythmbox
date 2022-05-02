client_c_args = [
  '-DGNOMELOCALEDIR="' + datadir + '/locale"',
  '-DG_LOG_DOMAIN="Rhythmbox"',
  '-DPIXMAP_DIR="' + datadir + '/pixmaps"',
  '-DSHARE_DIR="' + pkgdatadir + '"',
  '-DDATADIR="' + datadir + '"',
]

executable('rhythmbox-client',
    'rb-client.c',
    c_args: client_c_args,
    dependencies: [librb_dep, intl],
    install: true
)
