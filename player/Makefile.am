## arch-tag: Automake rules for playback library

noinst_LTLIBRARIES = librbplayer.la

BUILT_SOURCES =

librbplayer_la_SOURCES =	\
	rb-player.h 		\
	rb-recorder.h 

if USE_XINE
librbplayer_la_SOURCES += rb-player-xine.c rb-recorder-xine.c
else
librbplayer_la_SOURCES += rb-player-gst.c rb-recorder-gst.c
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
	-I$(top_srcdir)/metadata			\
	$(RHYTHMBOX_CFLAGS)

rb-recorder-marshal.c: rb-recorder-marshal.list
	echo "#include \"rb-recorder-marshal.h\"" > $@ && \
	@GLIB_GENMARSHAL@ $< --prefix=rb_recorder_marshal --body >> $@

rb-recorder-marshal.h: rb-recorder-marshal.list
	@GLIB_GENMARSHAL@ $< --prefix=rb_recorder_marshal --header > $@

BUILT_SOURCES += rb-recorder-marshal.c rb-recorder-marshal.h

librbplayer_la_SOURCES += $(BUILT_SOURCES)

EXTRA_DIST = rb-recorder-marshal.list

CLEANFILES = $(BUILT_SOURCES)
