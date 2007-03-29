## arch-tag: Automake rules for gstreamer backend library

noinst_LTLIBRARIES = librbbackendsgstreamer.la

librbbackendsgstreamer_la_SOURCES =			\
	rb-player-gst.h					\
	rb-player-gst.c					\
	$(NULL)

librbbackendsgstreamer_la_LIBADD =			\
	$(RHYTHMBOX_LIBS)

librbbackendsgstreamer_la_LDFLAGS = -export-dynamic

INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"			\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	-I$(top_srcdir)/metadata			\
	-I$(top_srcdir)/rhythmdb			\
	-I$(top_srcdir)/backends			\
	-I$(top_builddir)/lib 				\
	$(RHYTHMBOX_CFLAGS)

if ENABLE_TRACK_TRANSFER
INCLUDES += $(GNOME_MEDIA_PROFILES_CFLAGS)
librbbackendsgstreamer_la_SOURCES +=	\
	rb-encoder-gst.h		\
	rb-encoder-gst.c
endif

if USE_GSTREAMER_0_10_XFADE
librbbackendsgstreamer_la_SOURCES +=	\
	rb-player-gst-xfade.h		\
	rb-player-gst-xfade.c
endif
