## arch-tag: Automake rules for RhythmDB library

noinst_LTLIBRARIES = librhythmdb.la

librhythmdb_la_LDFLAGS = -export-dynamic
librhythmdb_la_LIBADD = 				\
	$(GIO_LIBS)					

INCLUDES = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"RhythmDB\"			\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	-I$(top_srcdir)/widgets				\
	-I$(top_srcdir)/metadata			\
	-I$(top_builddir)/lib                           \
	$(RHYTHMBOX_CFLAGS)				\
	$(GIO_CFLAGS)					\
	$(NO_STRICT_ALIASING_CFLAGS)

librhythmdb_la_SOURCES =				\
	rb-refstring.c					\
	rb-refstring.h					\
	rhythmdb.h					\
	rhythmdb-private.h				\
	rhythmdb.c					\
	rhythmdb-monitor.c				\
	rhythmdb-query.c				\
	rhythmdb-property-model.h			\
	rhythmdb-property-model.c			\
	rhythmdb-query-model.h				\
	rhythmdb-query-model.c				\
	rhythmdb-query-results.h			\
	rhythmdb-query-results.c			\
	rhythmdb-import-job.h				\
	rhythmdb-import-job.c


if USE_TREEDB
librhythmdb_la_SOURCES += rhythmdb-tree.h rhythmdb-tree.c
endif
