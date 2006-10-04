plugindir = $(PLUGINDIR)/audiocd
plugin_LTLIBRARIES = libaudiocd.la

libaudiocd_la_SOURCES =					\
	rb-audiocd-plugin.c				\
	../../sources/rb-audiocd-source.c		\
	../../sources/rb-audiocd-source.h

libaudiocd_la_LDFLAGS = $(PLUGIN_LIBTOOL_FLAGS)

INCLUDES = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib                        	\
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
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	$(TOTEM_PLPARSER_CFLAGS)			\
	$(HAL_CFLAGS)					\
	$(RHYTHMBOX_CFLAGS)				\
	$(LIBNAUTILUS_BURN_CFLAGS)			\
	$(WARN_CFLAGS)					\
	-D_XOPEN_SOURCE -D_BSD_SOURCE

gladedir = $(datadir)/rhythmbox/glade
glade_DATA =

plugin_in_files = audiocd.rb-plugin.desktop.in

%.rb-plugin: %.rb-plugin.desktop.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

plugin_DATA = $(plugin_in_files:.rb-plugin.desktop.in=.rb-plugin)

EXTRA_DIST = $(glade_DATA) $(plugin_in_files) ../../metadata/sj-metadata-marshal.list

MARSHALFILES = 

if HAVE_MUSICBRAINZ
libaudiocd_la_SOURCES += \
	../../metadata/sj-metadata.h ../../metadata/sj-metadata.c \
	../../metadata/sj-metadata-musicbrainz.h ../../metadata/sj-metadata-musicbrainz.c \
	../../metadata/sj-structures.h ../../metadata/sj-structures.c \
	../../metadata/sj-error.h ../../metadata/sj-error.c \
	$(MARSHALFILES)

libaudiocd_la_LIBADD = $(MUSICBRAINZ_LIBS)

MARSHALFILES += sj-metadata-marshal.h sj-metadata-marshal.c

sj-metadata-marshal.h: ../../metadata/sj-metadata-marshal.list
	( $(GLIB_GENMARSHAL) --prefix=metadata_marshal $< \
	--header > marshal-header.tmp \
	&& mv marshal-header.tmp $@ ) \
	|| ( rm -f marshal-header.tmp && exit 1 )

sj-metadata-marshal.c: ../../metadata/sj-metadata-marshal.list
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

