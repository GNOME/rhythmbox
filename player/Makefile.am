## arch-tag: Automake rules for playback library

noinst_LTLIBRARIES = librbplayer.la

librbplayer_la_SOURCES = rb-player.h

if USE_XINE
librbplayer_la_SOURCES += rb-player-xine.c
else
librbplayer_la_SOURCES += rb-player-gst.c
endif

librbplayer_la_LIBADD =				\
	$(top_builddir)/lib/librb.la		\
	$(RHYTHMBOX_LIBS)

librbplayer_la_LDFLAGS = -export-dynamic

INCLUDES = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"			\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	$(RHYTHMBOX_CFLAGS)

