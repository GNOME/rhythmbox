plugindir = $(PLUGINDIR)/audiocd
plugin_LTLIBRARIES = libaudiocd.la

libaudiocd_la_SOURCES =					\
	rb-audiocd-plugin.c				\
	rb-audiocd-source.c				\
	rb-audiocd-source.h

libaudiocd_la_LDFLAGS = $(PLUGIN_LIBTOOL_FLAGS)
libaudiocd_la_LIBTOOLFLAGS = --tag=disable-static

libaudiocd_la_LIBADD = \
	$(top_builddir)/shell/librhythmbox-core.la	\
	$(TOTEM_PLPARSER_LIBS)				\
	$(GSTCDDA_LIBS)

INCLUDES = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib                             \
	-I$(top_srcdir)/lib/libmediaplayerid            \
	-I$(top_srcdir)/metadata                       	\
	-I$(top_srcdir)/player                       	\
	-I$(top_srcdir)/rhythmdb                       	\
	-I$(top_srcdir)/widgets                    	\
	-I$(top_srcdir)/sources                    	\
	-I$(top_srcdir)/iradio                    	\
	-I$(top_srcdir)/podcast                    	\
	-I$(top_srcdir)/remote				\
	-I$(top_builddir)/remote			\
	-I$(top_srcdir)/plugins				\
	-I$(top_srcdir)/shell				\
	-I$(top_srcdir)/backends			\
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	$(TOTEM_PLPARSER_CFLAGS)			\
	$(RHYTHMBOX_CFLAGS)				\
	$(GSTCDDA_CFLAGS)				\
	-DUSE_TOTEM_PL_PARSER				\
	-D_XOPEN_SOURCE -D_BSD_SOURCE

if HAVE_MUSICBRAINZ
libaudiocd_la_LIBADD += $(MUSICBRAINZ_LIBS)
INCLUDES += $(MUSICBRAINZ_CFLAGS)
endif
if HAVE_MUSICBRAINZ3
libaudiocd_la_LIBADD +=$(MUSICBRAINZ3_LIBS)
INCLUDES += $(MUSICBRAINZ3_CFLAGS)
endif

libaudiocd_la_LIBADD += $(NULL)


gtkbuilderdir = $(plugindir)
gtkbuilder_DATA = 					\
	album-info.ui					\
	multiple-album.ui

uixmldir = $(plugindir)
uixml_DATA = audiocd-ui.xml

plugin_in_files = audiocd.rb-plugin.in

%.rb-plugin: %.rb-plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

plugin_DATA = $(plugin_in_files:.rb-plugin.in=.rb-plugin)

EXTRA_DIST = $(gtkbuilder_DATA) $(uixml_DATA) $(plugin_in_files) sj-metadata-marshal.list

SJ_FILES =									\
	sj-error.c sj-error.h							\
	sj-metadata.c sj-metadata.h						\
	sj-metadata-gvfs.c sj-metadata-gvfs.h					\
	sj-metadata-getter.c sj-metadata-getter.h				\
	sj-metadata-musicbrainz3.c sj-metadata-musicbrainz3.h			\
	sj-metadata-musicbrainz.c sj-metadata-musicbrainz.h			\
	sj-structures.c sj-structures.h						\
	sj-metadata-marshal.list

EGGDIR=$(srcdir)/../../../sound-juicer/libjuicer
regenerate-built-sources:
	EGGFILES="$(SJ_FILES)" EGGDIR="$(EGGDIR)" $(srcdir)/update-from-egg.sh || true

MARSHALFILES = 

if HAVE_SJ_METADATA_GETTER
libaudiocd_la_SOURCES +=							\
	sj-metadata.h sj-metadata.c						\
	sj-metadata-getter.c sj-metadata-getter.h				\
	sj-metadata-gvfs.c sj-metadata-gvfs.h					\
	sj-structures.h sj-structures.c						\
	sj-error.h sj-error.c							\
	$(MARSHALFILES)

if HAVE_MUSICBRAINZ
libaudiocd_la_SOURCES += sj-metadata-musicbrainz.c sj-metadata-musicbrainz.h
endif

if HAVE_MUSICBRAINZ3
libaudiocd_la_SOURCES += sj-metadata-musicbrainz3.c sj-metadata-musicbrainz3.h
endif

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

CLEANFILES = $(plugin_DATA) $(BUILT_SOURCES)
DISTCLEANFILES = $(plugin_DATA)

