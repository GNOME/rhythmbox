## arch-tag: Automake rules for MonkeyMedia sound library

SUBDIRS = stream-info-impl

noinst_LTLIBRARIES = libmonkey-media.la

BUILT_SOURCES =						\
	monkey-media-marshal.c				\
	monkey-media-marshal.h				\
	monkey-media-includes.h

libmonkey_media_la_SOURCES =				\
	monkey-media.h					\
	monkey-media-stream-info.h			\
	monkey-media-includes.h				\
	monkey-media.c					\
	monkey-media-stream-info.c			\
	monkey-media-private.h				\
	$(BUILT_SOURCES)

libmonkey_media_la_LIBADD = 					    \
	$(top_builddir)/lib/librb.la \
	$(top_builddir)/metadata/monkey-media/stream-info-impl/libstream-info-impl.la \
	$(MONKEYMEDIA_LIBS)

libmonkey_media_la_LDFLAGS = -export-dynamic

INCLUDES = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"MonkeyMedia\"			\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	-I$(top_srcdir)/metadata/monkey-media/stream-info-impl		\
	$(RHYTHMBOX_CFLAGS)

monkey-media-marshal.h: monkey-media-marshal.list $(GLIB_GENMARSHAL)
	$(GLIB_GENMARSHAL) $< --header --prefix=monkey_media_marshal > $@

monkey-media-marshal.c: monkey-media-marshal.list monkey-media-marshal.h $(GLIB_GENMARSHAL)
	echo "#include \"monkey-media-marshal.h\"" > $@ && \
	$(GLIB_GENMARSHAL) $< --body --prefix=monkey_media_marshal >> $@

EXTRA_DIST = monkey-media-marshal.list

CLEANFILES = $(BUILT_SOURCES)
