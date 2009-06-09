SUBDIRS = 						\
	audiocd						\
	audioscrobbler					\
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
else
if HAVE_NAUTILUS_BURN
SUBDIRS += cd-recorder
endif # HAVE_NAUTILUS_BURN
endif # HAVE_LIBBRASERO_MEDIA

if USE_DAAP
SUBDIRS += daap
endif

if USE_MTP
SUBDIRS += mtpdevice
endif

if ENABLE_FM_RADIO
SUBDIRS += fmradio
endif

