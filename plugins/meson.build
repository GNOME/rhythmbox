plugin_headers = ['rb-plugin-macros.h']

install_headers(plugin_headers, subdir: 'rhythmbox/plugins')

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
  subdir('lyrics')
  subdir('magnatune')
  subdir('pythonconsole')
  subdir('rbzeitgeist')
  subdir('replaygain')
  subdir('sendto')
  subdir('soundcloud')
  subdir('webremote')
  subdir('rb')

  subdir('context')
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
