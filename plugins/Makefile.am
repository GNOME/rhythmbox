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
	power-manager					\
	sample

if ENABLE_PYTHON
SUBDIRS +=						\
	artsearch					\
	context						\
	im-status					\
	lyrics						\
	magnatune					\
	pythonconsole					\
	rbzeitgeist					\
	replaygain					\
	sample-python					\
	sendto						\
	rb

endif

if ENABLE_VALA
SUBDIRS +=						\
	sample-vala
endif

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

if USE_CLUTTER
SUBDIRS += visualizer
endif

if USE_NOTIFY
SUBDIRS += notification
endif

if ENABLE_GRILO
SUBDIRS += grilo
endif
