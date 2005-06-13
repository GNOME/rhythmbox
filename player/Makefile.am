## arch-tag: Automake rules for playback library

noinst_LTLIBRARIES = librbplayer.la

librbplayer_la_SOURCES =	\
	rb-player.h 		\
	rb-player-gst.c

librbplayer_la_LIBADD =				\
	$(RHYTHMBOX_LIBS)

librbplayer_la_LDFLAGS = -export-dynamic

INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"			\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	-I$(top_srcdir)/metadata			\
	$(RHYTHMBOX_CFLAGS)


BUILT_SOURCES = rb-recorder-marshal.c rb-recorder-marshal.h

if USE_CD_BURNER
librbplayer_la_SOURCES += 	\
	rb-recorder.h		\
	rb-recorder-gst.c       \
	rb-recorder-marshal.c	\
	rb-recorder-marshal.h

INCLUDES += $(LIBNAUTILUS_BURN_CFLAGS)
endif

rb-recorder-marshal.c: rb-recorder-marshal.list
	echo "#include \"rb-recorder-marshal.h\"" > $@ && \
	@GLIB_GENMARSHAL@ $< --prefix=rb_recorder_marshal --body >> $@

rb-recorder-marshal.h: rb-recorder-marshal.list
	@GLIB_GENMARSHAL@ $< --prefix=rb_recorder_marshal --header > $@


EXTRA_DIST = rb-recorder-marshal.list

CLEANFILES = $(BUILT_SOURCES)
