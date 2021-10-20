plugin_headers = ['rb-plugin-macros.h']

install_headers(plugin_headers, subdir: 'rhythmbox/plugins')

po_dir = meson.project_source_root() / 'po'
# FIXME: Move custom_target to i18n.merge_file when gettext acquires plugin support.
#        http://lists.gnu.org/archive/html/bug-gettext/2017-06/msg00001.html
msgfmt_plugin_cmd = [
  find_program('msgfmt'),
  '--desktop',
  '--keyword=Name',
  '--keyword=Description',
  '--template=@INPUT@',
  '-d', po_dir,
  '--output=@OUTPUT@'
]

subdir('audiocd')
subdir('audioscrobbler')
subdir('dbus-media-server')
subdir('generic-player')
subdir('iradio')
subdir('mmkeys')
subdir('mpris')
subdir('power-manager')

if enable_python
  subdir('artsearch')
  subdir('im-status')
  subdir('listenbrainz')
  subdir('lyrics')
  subdir('magnatune')
  subdir('pythonconsole')
  subdir('rbzeitgeist')
  subdir('replaygain')
  subdir('soundcloud')
  subdir('webremote')
  subdir('rb')

  # Disabled since 67b24c9c6b3b8d9b95924b319407eb770c9bab11
  # subdir('context')
endif

if use_lirc
  subdir('lirc')
endif

if use_ipod
  subdir('ipod')
endif

if have_libbrasero_media
  subdir('brasero-disc-recorder')
endif

if have_libdmapsharing
  subdir('daap')
endif

if use_mtp
  subdir('mtpdevice')
endif

if enable_fm_radio
  subdir('fmradio')
endif

if use_notify
  subdir('notification')
endif

if enable_grilo
  subdir('grilo')
endif

if use_gudev
  subdir('android')
endif
