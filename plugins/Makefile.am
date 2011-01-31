SUBDIRS = 						\
	audiocd						\
	generic-player					\
	iradio						\
	mmkeys						\
	power-manager					\
	sample						\
	status-icon					\
	visualizer

if WITH_LIRC
SUBDIRS += lirc
endif

if ENABLE_PYTHON
SUBDIRS += 						\
	pythonconsole					\
	sample-python					\
	artdisplay					\
	lyrics						\
	magnatune					\
	jamendo						\
	coherence					\
	im-status					\
	context						\
	sendto						\
	replaygain					\
	rbzeitgeist					\
	rb
endif

if ENABLE_VALA
SUBDIRS +=						\
	sample-vala
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

if USE_GDBUS
SUBDIRS += 						\
	dbus-media-server				\
	mpris
endif

if ENABLE_LASTFM
SUBDIRS += audioscrobbler
endif
