## arch-tag: Automake rules for metadata library

SUBDIRS =

noinst_LTLIBRARIES = librbmetadata.la

librbmetadata_la_SOURCES = rb-metadata.h rb-metadata-common.c rb-metadata-gst.c

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

