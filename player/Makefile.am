## arch-tag: Automake rules for playback library

noinst_LTLIBRARIES = librbplayer.la

librbplayer_la_SOURCES =	\
	rb-player.h 		\
	rb-player-gst.c		\
	rb-recorder.h		\
	rb-recorder-gst.c

librbplayer_la_LIBADD =				\
	$(RHYTHMBOX_LIBS)

librbplayer_la_LDFLAGS = -export-dynamic

INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"			\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	-I$(top_srcdir)/metadata			\
	-I$(top_srcdir)/sources				\
	-I$(top_srcdir)/shell				\
	-I$(top_srcdir)/rhythmdb			\
	-I$(top_srcdir)/widgets				\
	-I$(top_srcdir)/daapsharing			\
	-I$(top_builddir)/lib                           \
	$(LIBNAUTILUS_BURN_CFLAGS)			\
	$(RHYTHMBOX_CFLAGS)


if USE_DAAP
INCLUDES += -I$(top_srcdir)/daapsharing			
endif

