## arch-tag: Automake rules for RhythmDB library

noinst_LTLIBRARIES = librhythmdb.la

librhythmdb_la_LDFLAGS = -export-dynamic
librhythmdb_la_LIBADD = $(top_builddir)/metadata/librbmetadata.la 

INCLUDES = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"RhythmDB\"			\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	-I$(top_srcdir)/metadata			\
	$(RHYTHMBOX_CFLAGS)

librhythmdb_la_SOURCES =				\
	rhythmdb.h					\
	rhythmdb.c					\
	rhythmdb-property-model.h			\
	rhythmdb-property-model.c			\
	rhythmdb-query-model.h				\
	rhythmdb-query-model.c



if USE_TREEDB
librhythmdb_la_SOURCES += rhythmdb-tree.h rhythmdb-tree.c
endif



