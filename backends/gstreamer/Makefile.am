noinst_LTLIBRARIES = librbbackendsgstreamer.la

librbbackendsgstreamer_la_SOURCES =			\
	rb-encoder-gst.h				\
	rb-encoder-gst.c				\
	rb-player-gst-helper.c				\
	rb-player-gst-helper.h				\
	rb-player-gst.h					\
	rb-player-gst.c					\
	rb-player-gst-xfade.h				\
	rb-player-gst-xfade.c				\
	$(NULL)

librbbackendsgstreamer_la_LIBADD =			\
	-lgstpbutils-1.0				\
	-lgstcontroller-1.0
	$(RHYTHMBOX_LIBS)

librbbackendsgstreamer_la_LDFLAGS = -export-dynamic

AM_CPPFLAGS = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"			\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	-I$(top_srcdir)/metadata			\
	-I$(top_srcdir)/rhythmdb			\
	-I$(top_srcdir)/backends			\
	-I$(top_builddir)/lib 				\
	$(RHYTHMBOX_CFLAGS)

