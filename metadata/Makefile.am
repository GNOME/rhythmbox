## arch-tag: Automake rules for metadata library

SUBDIRS =

noinst_LTLIBRARIES = librbmetadata.la

librbmetadata_la_SOURCES = rb-metadata.h

librbmetadata_la_LIBADD =			\
	$(top_builddir)/lib/librb.la		\
	$(RHYTHMBOX_LIBS)

librbmetadata_la_LDFLAGS = -export-dynamic

INCLUDES = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"			\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	$(RHYTHMBOX_CFLAGS)

##if USE_XINE
SUBDIRS += monkey-media
librbmetadata_la_LIBADD += $(top_builddir)/metadata/monkey-media/libmonkey-media.la
librbmetadata_la_SOURCES += rb-metadata-mm.c
INCLUDES += -I$(top_srcdir)/metadata/monkey-media
##else
##librbmetadata_la_SOURCES += rb-metadata-gst.c
##endif


##if USE_XINE
##endif
