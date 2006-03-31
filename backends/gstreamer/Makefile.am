## arch-tag: Automake rules for gstreamer backend library

noinst_LTLIBRARIES = librbbackendsgstreamer.la

librbbackendsgstreamer_la_SOURCES =	\
	rb-encoder-gst.h		\
	rb-encoder-gst.c

librbbackendsgstreamer_la_LIBADD =				\
	$(RHYTHMBOX_LIBS)

librbbackendsgstreamer_la_LDFLAGS = -export-dynamic

INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"			\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	-I$(top_srcdir)/metadata			\
	-I$(top_srcdir)/sources				\
	-I$(top_srcdir)/rhythmdb			\
	-I$(top_srcdir)/backends			\
	-I$(top_srcdir)/widgets				\
	$(LIBJUICER_CFLAGS)				\
	$(RHYTHMBOX_CFLAGS)


