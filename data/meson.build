po_dir = meson.project_source_root() / 'po'

desktop_files = [ 'org.gnome.Rhythmbox3.desktop', 'org.gnome.Rhythmbox3.device.desktop' ]
foreach desktop_file : desktop_files
  desktop_file = i18n.merge_file(
    input: desktop_file + '.in',
    output: desktop_file,
    install: true,
    install_dir: datadir / 'applications',
    po_dir: po_dir,
    type: 'desktop',
  )
endforeach

service_conf = configuration_data()
service_conf.set('bindir', get_option('prefix') / get_option('bindir'))

service = 'org.gnome.Rhythmbox3.service'

configure_file(
  input: service + '.in',
  output: service,
  install: true,
  install_dir: datadir / 'dbus-1/services',
  configuration: service_conf
)

install_data('org.gnome.rhythmbox.gschema.xml',
  install_dir: datadir / 'glib-2.0' / 'schemas')

glib_compile_schemas = find_program('glib-compile-schemas', required: true)
gschemas_compiled = custom_target('gschemas.compiled',
  input: 'org.gnome.rhythmbox.gschema.xml',
  output: 'gschemas.compiled',
  command: [glib_compile_schemas, '--targetdir=' + meson.current_build_dir(), meson.current_source_dir()]
)

playlists_xml = i18n.merge_file(
  input: 'playlists.xml.in',
  output: 'playlists.xml',
  po_dir: po_dir,
  data_dirs: meson.project_source_root() / 'data'
)

install_data('rhythmbox.gep',
  install_dir: datadir / 'rhythmbox')


mans = [
  'rhythmbox.1',
  'rhythmbox-client.1'
]

install_man(mans)

appdata = 'org.gnome.Rhythmbox3.appdata.xml'

appdata_file = i18n.merge_file(
  input: appdata + '.in',
  output: appdata,
  install: true,
  install_dir: datadir / 'metainfo',
  po_dir: po_dir,
  type: 'xml',
)

# Validate Appdata
appstream_util = find_program('appstream-util', required: false)
if appstream_util.found()
  test(
      'validate-appdata', appstream_util,
      depends: appdata_file,
      args: ['validate', '--nonet', appdata_file.full_path()]
      )
endif
