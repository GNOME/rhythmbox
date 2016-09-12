desktop_in_files = [ 'rhythmbox.desktop.in', 'rhythmbox-device.desktop.in' ]
foreach desktop_in_file : desktop_in_files
  configure_file(
    input: desktop_in_file + '.in',
    output: desktop_in_file,
    configuration: cdata
  )
endforeach

# It would be nice to avoid requiring intltool, and just use Gettext's msgfmt
# tool. The blocker is we need to write an appropriate .its file to describe
# how to translate Rhythmbox's XML playlist format, because msgfmt doesn't
# support the Intltool 'generic XML style' that we currently use.
playlists_xml = custom_target('playlists.xml',
  input: 'playlists.xml.in',
  output: 'playlists.xml',
  command: [intltool_merge, '--quiet', '--xml-style', '--utf8', '../po', '@INPUT@', '@OUTPUT@'],
)
