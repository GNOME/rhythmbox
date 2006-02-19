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
	$(RHYTHMBOX_LIBS)

librbmetadata_la_LDFLAGS = -export-dynamic

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
	$(RHYTHMBOX_LIBS)				\
	$(DBUS_LIBS)					\
	$(top_builddir)/metadata/librbmetadatasvc.la	\
	$(top_builddir)/lib/librb.la

# test program?
noinst_PROGRAMS = test-metadata
test_metadata_SOURCES =					\
	test-metadata.c

test_metadata_LDADD =					\
	$(RHYTHMBOX_LIBS)				\
	$(DBUS_LIBS)					\
	$(top_builddir)/metadata/librbmetadata.la	\
	$(top_builddir)/lib/librb.la

else

noinst_LTLIBRARIES = librbmetadata.la

librbmetadata_la_SOURCES = \
	rb-metadata.h rb-metadata-common.c rb-metadata-gst.c


librbmetadata_la_LIBADD =			\
	$(RHYTHMBOX_LIBS)

librbmetadata_la_LDFLAGS = -export-dynamic

endif

MARSHALFILES = 

if HAVE_MUSICBRAINZ
librbmetadata_la_SOURCES += \
	sj-metadata.h sj-metadata.c \
	sj-metadata-musicbrainz.h sj-metadata-musicbrainz.c \
	sj-structures.h sj-structures.c \
	sj-error.h sj-error.c \
	$(MARSHALFILES)

INCLUDES += $(LIBNAUTILUS_BURN_CFLAGS)

MARSHALFILES += sj-metadata-marshal.h sj-metadata-marshal.c

sj-metadata-marshal.h: sj-metadata-marshal.list
	( $(GLIB_GENMARSHAL) --prefix=metadata_marshal $< \
	--header > marshal-header.tmp \
	&& mv marshal-header.tmp $@ ) \
	|| ( rm -f marshal-header.tmp && exit 1 )

sj-metadata-marshal.c: sj-metadata-marshal.list
	( $(GLIB_GENMARSHAL) --prefix=metadata_marshal $< \
	--body > marshal-source.tmp \
	&& echo "#include \"sj-metadata-marshal.h\"" > $@ \
	&& cat marshal-source.tmp >> $@ \
	&& rm -f marshal-source.tmp ) \
	|| ( rm -f marshal-source.tmp && exit 1 )

endif

BUILT_SOURCES = $(MARSHALFILES)
CLEANFILES = $(BUILT_SOURCES)
EXTRA_DIST = sj-metadata-marshal.list

