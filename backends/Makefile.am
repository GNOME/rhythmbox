## arch-tag: Automake rules for backends library

noinst_LTLIBRARIES = librbbackends.la

SUBDIRS = gstreamer

librbbackends_la_SOURCES =	\
	rb-encoder.h 		\
	rb-encoder.c

librbbackends_la_LIBADD =				\
	$(RHYTHMBOX_LIBS)				\
	$(top_builddir)/backends/gstreamer/librbbackendsgstreamer.la

librbbackends_la_LDFLAGS = -export-dynamic

INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"			\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	-I$(top_srcdir)/metadata			\
	-I$(top_srcdir)/shell				\
	-I$(top_srcdir)/rhythmdb			\
	-I$(top_srcdir)/widgets				\
	-I$(top_srcdir)/backends/gstreamer		\
	$(LIBNAUTILUS_BURN_CFLAGS)			\
	$(RHYTHMBOX_CFLAGS)

