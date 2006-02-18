## arch-tag: Automake rules for RhythmDB library

noinst_LTLIBRARIES = librhythmdb.la

librhythmdb_la_LDFLAGS = -export-dynamic
librhythmdb_la_LIBADD = 				\
	$(GDA_LIBS)

INCLUDES = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"RhythmDB\"			\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	-I$(top_srcdir)/metadata			\
	$(GDA_CFLAGS)					\
	$(RHYTHMBOX_CFLAGS)

librhythmdb_la_SOURCES =				\
	$(BUILT_SOURCES)				\
	gsequence.c					\
	gsequence.h					\
	rb-refstring.c					\
	rb-refstring.h					\
	rhythmdb.h					\
	rhythmdb-private.h				\
	rhythmdb.c					\
	rhythmdb-property-model.h			\
	rhythmdb-property-model.c			\
	rhythmdb-query-model.h				\
	rhythmdb-query-model.c				\
	rhythmdb-query-results.h			\
	rhythmdb-query-results.c


if USE_TREEDB
librhythmdb_la_SOURCES += rhythmdb-tree.h rhythmdb-tree.c
endif

if USE_GDADB
librhythmdb_la_SOURCES += rhythmdb-gda.h rhythmdb-gda.c
endif

BUILT_SOURCES=rhythmdb-marshal.c rhythmdb-marshal.h

CLEAN_FILES = $(BUILT_SOURCES)

rhythmdb-marshal.c: rhythmdb-marshal.list
	@GLIB_GENMARSHAL@ --prefix=rhythmdb_marshal $(srcdir)/rhythmdb-marshal.list --header --body > rhythmdb-marshal.c

rhythmdb-marshal.h: rhythmdb-marshal.list
	@GLIB_GENMARSHAL@ --prefix=rhythmdb_marshal $(srcdir)/rhythmdb-marshal.list --header > rhythmdb-marshal.h

EXTRA_DIST = rhythmdb-marshal.list
