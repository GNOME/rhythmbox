pluginincludedir = $(includedir)/rhythmbox/plugins
plugininclude_HEADERS = rb-plugin-macros.h

SUBDIRS =						\
	audiocd						\
	audioscrobbler					\
	dbus-media-server				\
	generic-player					\
	iradio						\
	mmkeys						\
	mpris						\
	power-manager

if ENABLE_PYTHON
SUBDIRS +=						\
	artsearch					\
	im-status					\
	listenbrainz					\
	lyrics						\
	magnatune					\
	pythonconsole					\
	rbzeitgeist					\
	replaygain					\
	soundcloud					\
	webremote					\
	rb

SUBDIRS += context

endif # ENABLE_PYTHON

if WITH_LIRC
SUBDIRS += lirc
endif

if USE_IPOD
SUBDIRS += ipod
endif

if HAVE_LIBBRASERO_MEDIA
SUBDIRS += brasero-disc-recorder
endif

if USE_LIBDMAPSHARING
SUBDIRS += daap
endif

if USE_MTP
SUBDIRS += mtpdevice
endif

if ENABLE_FM_RADIO
SUBDIRS += fmradio
endif

if USE_NOTIFY
SUBDIRS += notification
endif

if ENABLE_GRILO
SUBDIRS += grilo
endif

if USE_GUDEV
SUBDIRS += android
endif
