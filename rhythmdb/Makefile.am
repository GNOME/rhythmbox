noinst_LTLIBRARIES = librhythmdb.la

librhythmdb_la_LDFLAGS = -export-dynamic
librhythmdb_la_LIBADD = -lm

INCLUDES = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"RhythmDB\"			\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	-I$(top_srcdir)/widgets				\
	-I$(top_srcdir)/metadata			\
	-I$(top_srcdir)/podcast				\
	-I$(top_builddir)/lib                           \
	$(RHYTHMBOX_CFLAGS)				\
	$(NO_STRICT_ALIASING_CFLAGS)

rhythmdbincludedir = $(includedir)/rhythmbox/rhythmdb
rhythmdbinclude_HEADERS =				\
	rb-refstring.h					\
	rhythmdb.h					\
	rhythmdb-property-model.h			\
	rhythmdb-query-model.h				\
	rhythmdb-query-result-list.h			\
	rhythmdb-query-results.h			\
	rhythmdb-import-job.h				\
	rhythmdb-entry.h				\
	rhythmdb-entry-type.h

librhythmdb_la_SOURCES =				\
	$(rhythmdbinclude_HEADERS)			\
	rb-refstring.c					\
	rhythmdb-private.h				\
	rhythmdb.c					\
	rhythmdb-monitor.c				\
	rhythmdb-query.c				\
	rhythmdb-property-model.c			\
	rhythmdb-query-model.c				\
	rhythmdb-query-result-list.c			\
	rhythmdb-query-results.c			\
	rhythmdb-import-job.c				\
	rhythmdb-entry-type.c				\
	rhythmdb-song-entry-types.c


if USE_TREEDB
librhythmdb_la_SOURCES += rhythmdb-tree.h rhythmdb-tree.c
endif
