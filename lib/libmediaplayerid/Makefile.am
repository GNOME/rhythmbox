
noinst_LTLIBRARIES = libmediaplayerid.la

libmediaplayerid_la_SOURCES =		\
	mediaplayerid.h			\
	mpid-private.h			\
	mpid-device.c			\
	mpid-files.c			\
	mpid-util.c

INCLUDES =				\
	-I$(top_srcdir)			\
	$(RHYTHMBOX_CFLAGS)

# use the GUdev/media-player-id implementation if possible,
# otherwise HAL.

if USE_GUDEV

libmediaplayerid_la_SOURCES += mpid-udev.c
EXTRA_DIST = mpid-hal.c mpid-dummy.c

INCLUDES += $(GUDEV_CFLAGS)
libmediaplayerid_la_LIBADD = $(GUDEV_LIBS)

else

if HAVE_HAL

libmediaplayerid_la_SOURCES += mpid-hal.c
EXTRA_DIST = mpid-udev.c mpid-dummy.c

INCLUDES += $(HAL_CFLAGS)
libmediaplayerid_la_LIBADD = $(HAL_LIBS)

else

libmediaplayerid_la_SOURCES += mpid-dummy.c
EXTRA_DIST = mpid-udev.c mpid-hal.c

endif #  HAVE_HAL

endif #  HAVE_GUDEV

