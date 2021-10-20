
noinst_LTLIBRARIES = libmediaplayerid.la

mpidincludedir = $(includedir)/rhythmbox/lib/libmediaplayerid
mpidinclude_HEADERS = 			\
	mediaplayerid.h

libmediaplayerid_la_SOURCES =		\
	$(mpidinclude_HEADERS)		\
	mpid-private.h			\
	mpid-device.c			\
	mpid-files.c			\
	mpid-util.c

AM_CPPFLAGS =				\
	-I$(top_srcdir)			\
	$(RHYTHMBOX_CFLAGS)

# use the GUdev/media-player-id implementation if possible.

if USE_GUDEV

libmediaplayerid_la_SOURCES += mpid-udev.c
EXTRA_DIST = mpid-dummy.c

AM_CPPFLAGS += $(GUDEV_CFLAGS)
libmediaplayerid_la_LIBADD = $(GUDEV_LIBS)

else

libmediaplayerid_la_SOURCES += mpid-dummy.c
EXTRA_DIST = mpid-udev.c

endif #  HAVE_GUDEV

