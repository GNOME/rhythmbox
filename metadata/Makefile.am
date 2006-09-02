## arch-tag: Automake rules for metadata library

SUBDIRS =

INCLUDES = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"			\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	-I$(top_srcdir)/widgets				\
	$(RHYTHMBOX_CFLAGS)

if WITH_METADATA_HELPER

INCLUDES += $(DBUS_CFLAGS)				\
	-DLIBEXEC_DIR=\"$(libexecdir)\"

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
	rb-metadata-gst.c

libexec_PROGRAMS = rhythmbox-metadata
rhythmbox_metadata_SOURCES = 				\
	rb-metadata-dbus-service.c

rhythmbox_metadata_LDADD = 				\
	$(top_builddir)/metadata/librbmetadatasvc.la	\
	$(top_builddir)/lib/librb.la			\
	$(RHYTHMBOX_LIBS)				\
	$(DBUS_LIBS)

# test program?
noinst_PROGRAMS = test-metadata
test_metadata_SOURCES =					\
	test-metadata.c

test_metadata_LDADD =					\
	$(top_builddir)/metadata/librbmetadata.la	\
	$(top_builddir)/lib/librb.la			\
	$(RHYTHMBOX_LIBS)				\
	$(DBUS_LIBS)

else

noinst_LTLIBRARIES = librbmetadata.la

librbmetadata_la_SOURCES = \
	rb-metadata.h rb-metadata-common.c rb-metadata-gst.c


librbmetadata_la_LIBADD =			\
	$(RHYTHMBOX_LIBS)

endif

librbmetadata_la_LDFLAGS = -export-dynamic

