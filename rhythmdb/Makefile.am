## arch-tag: Automake rules for RhythmDB library

noinst_LTLIBRARIES = librhythmdb.la

librhythmdb_la_SOURCES =				\
	rhythmdb.h

if USE_TREEDB
librhythmdb_la_SOURCES += rhythmdb-tree.c
else
if USE_SQLITE
librhythmdb_la_SOURCES += rhythmdb-sqlite.c
else
endif
endif

librhythmdb_la_LDFLAGS = -export-dynamic

INCLUDES = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"RhythmDB\"			\
	-I$(top_srcdir) 				\
	$(RHYTHMBOX_CFLAGS)
