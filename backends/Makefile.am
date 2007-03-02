## arch-tag: Automake rules for backends library

noinst_LTLIBRARIES = librbbackends.la

SUBDIRS = gstreamer 

librbbackends_la_SOURCES =				\
	rb-player.h					\
	rb-player.c					\
	rb-player-gst-filter.h				\
	rb-player-gst-filter.c				\
	rb-player-gst-tee.h				\
	rb-player-gst-tee.c				\
	rb-player-gst-data-tee.h			\
	rb-player-gst-data-tee.c			\
	$(NULL)

librbbackends_la_LIBADD =				\
	gstreamer/librbbackendsgstreamer.la		\
	$(RHYTHMBOX_LIBS)

librbbackends_la_LDFLAGS = -export-dynamic

INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"			\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	-I$(top_srcdir)/metadata			\
	-I$(top_srcdir)/rhythmdb			\
	-I$(top_srcdir)/backends/gstreamer		\
	-I$(top_builddir)/lib 				\
	$(LIBNAUTILUS_BURN_CFLAGS)			\
	$(RHYTHMBOX_CFLAGS)

if ENABLE_TRACK_TRANSFER
librbbackends_la_SOURCES +=	\
	rb-encoder.h 		\
	rb-encoder.c
endif

