mpid_headers = [
  'mediaplayerid.h'
]

install_headers(mpid_headers, subdir: 'rhythmbox/lib/libmediaplayerid')

mpid_sources = files(
  'mpid-device.c',
  'mpid-files.c',
  'mpid-util.c'
)

mpid_dependencies = [gio, gio_unix, glib, gobject]

# use the GUdev/media-player-id implementation if possible,
# otherwise HAL.
if use_gudev
  mpid_sources += files('mpid-udev.c')
  mpid_dependencies += [gudev]
elif use_hal
  mpid_sources += files('mpid-hal.c')
  mpid_dependencies += [dbus_glib, hal]
else
  mpid_sources += files('mpid-dummy.c')
endif
