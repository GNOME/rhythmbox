subdir('icons')

po_dir = meson.project_source_root() / 'po'

desktop_files = [ 'org.gnome.Rhythmbox3.desktop', 'org.gnome.Rhythmbox3.device.desktop' ]
foreach desktop_file : desktop_files
  desktop_in_file = configure_file(
    input: desktop_file + '.in.in',
    output: desktop_file + '.in',
    configuration: cdata
  )
  desktop_file = custom_target(
    desktop_file,
    input: desktop_in_file,
    output: desktop_file,
    install: true,
    install_dir: datadir / 'applications',
    command: [intltool_merge, '--quiet', '--desktop-style', '--utf8', po_dir, '@INPUT@', '@OUTPUT@'],
)
endforeach

service_conf = configuration_data()
service_conf.set('bindir', get_option('bindir'))

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

# It would be nice to avoid requiring intltool, and just use Gettext's msgfmt
# tool. The blocker is we need to write an appropriate .its file to describe
# how to translate Rhythmbox's XML playlist format, because msgfmt doesn't
# support the Intltool 'generic XML style' that we currently use.
playlists_xml = custom_target('playlists.xml',
  input: 'playlists.xml.in',
  output: 'playlists.xml',
  command: [intltool_merge, '--quiet', '--xml-style', '--utf8', '../po', '@INPUT@', '@OUTPUT@'],
)

install_data('rhythmbox.gep',
  install_dir: datadir / 'rhythmbox')


mans = [
  'rhythmbox.1',
  'rhythmbox-client.1'
]

install_man(mans)

appdata = 'org.gnome.Rhythmbox3.appdata.xml'

appdata_file = custom_target('appdata',
  input: appdata + '.in',
  output: appdata,
  install: true,
  install_dir: datadir / 'metainfo',
  command: [intltool_merge, '--quiet', '--xml-style', '--utf8', po_dir, '@INPUT@', '@OUTPUT@'],
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
