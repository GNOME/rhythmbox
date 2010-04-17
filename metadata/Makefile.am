SUBDIRS =

INCLUDES = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"			\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	-I$(top_srcdir)/widgets				\
	$(RHYTHMBOX_CFLAGS)				\
	$(NO_STRICT_ALIASING_CFLAGS)

INSTALLED_METADATA_HELPER=`echo rhythmbox-metadata | sed '$(transform)'`

INCLUDES += $(DBUS_CFLAGS)				\
	-DLIBEXEC_DIR=\"$(libexecdir)\"			\
	-DINSTALLED_METADATA_HELPER=\"$(INSTALLED_METADATA_HELPER)\"

# API

metadataincludedir = $(includedir)/rhythmbox/metadata
metadatainclude_HEADERS = rb-metadata.h

# client library

noinst_LTLIBRARIES = librbmetadata.la

librbmetadata_la_SOURCES =				\
	rb-metadata.h					\
	rb-metadata-common.c				\
	rb-metadata-dbus.h				\
	rb-metadata-dbus.c				\
	rb-metadata-dbus-client.c

librbmetadata_la_LIBADD = 				\
	$(DBUS_LIBS)					\
	$(RHYTHMBOX_LIBS)

# service

noinst_LTLIBRARIES += librbmetadatasvc.la

librbmetadatasvc_la_SOURCES =				\
	rb-metadata.h					\
	rb-metadata-common.c				\
	rb-metadata-dbus.h				\
	rb-metadata-dbus.c				\
	rb-metadata-gst.c				\
	rb-metadata-gst-common.h			\
	rb-metadata-gst-common.c

libexec_PROGRAMS = rhythmbox-metadata
rhythmbox_metadata_SOURCES = 				\
	rb-metadata-dbus-service.c

rhythmbox_metadata_LDADD = 				\
	librbmetadatasvc.la				\
	$(top_builddir)/lib/librb.la			\
	$(RHYTHMBOX_LIBS)				\
	-lgstpbutils-0.10				\
	$(DBUS_LIBS)

# test program?
noinst_PROGRAMS = test-metadata
test_metadata_SOURCES =					\
	test-metadata.c

test_metadata_LDADD =					\
	librbmetadata.la				\
	$(top_builddir)/lib/librb.la			\
	$(RHYTHMBOX_LIBS)				\
	-lgstpbutils-0.10				\
	$(DBUS_LIBS)

librbmetadata_la_LDFLAGS = -export-dynamic

