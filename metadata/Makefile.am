## arch-tag: Automake rules for metadata library

SUBDIRS =

noinst_LTLIBRARIES = librbmetadata.la

librbmetadata_la_SOURCES = rb-metadata.h rb-metadata-common.c

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

if USE_MONKEYMEDIA
SUBDIRS += monkey-media
librbmetadata_la_LIBADD += $(top_builddir)/metadata/monkey-media/libmonkey-media.la
librbmetadata_la_SOURCES += rb-metadata-mm.c
INCLUDES += -I$(top_srcdir)/metadata/monkey-media
endif

if WITH_XINE_METADATA
librbmetadata_la_SOURCES += rb-metadata-xine.c
endif

if WITH_GST_METADATA
librbmetadata_la_SOURCES += rb-metadata-gst.c
endif
