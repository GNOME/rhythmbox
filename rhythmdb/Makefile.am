## arch-tag: Automake rules for RhythmDB library

noinst_PROGRAMS = test-rhythmdb-simple test-rhythmdb-indexing \
		  test-rhythmdb-query test-rhythmdb-tree-serialization \
		  test-rhythmdb-tree-deserialization

test_rhythmdb_simple_SOURCES = test-rhythmdb-simple.c
test_rhythmdb_simple_LDADD =					\
	$(top_builddir)/rhythmdb/librhythmdb.la			\
	$(top_builddir)/lib/librb.la				\
	$(RHYTHMBOX_LIBS)

test_rhythmdb_indexing_SOURCES = test-rhythmdb-indexing.c
test_rhythmdb_indexing_LDADD =					\
	$(top_builddir)/rhythmdb/librhythmdb.la			\
	$(top_builddir)/lib/librb.la				\
	$(RHYTHMBOX_LIBS)

test_rhythmdb_query_SOURCES = test-rhythmdb-query.c
test_rhythmdb_query_LDADD =					\
	$(top_builddir)/rhythmdb/librhythmdb.la			\
	$(top_builddir)/lib/librb.la				\
	$(RHYTHMBOX_LIBS)

test_rhythmdb_tree_serialization_SOURCES = test-rhythmdb-tree-serialization.c
test_rhythmdb_tree_serialization_LDADD =				\
	$(top_builddir)/rhythmdb/librhythmdb.la			\
	$(top_builddir)/lib/librb.la				\
	$(RHYTHMBOX_LIBS)

test_rhythmdb_tree_deserialization_SOURCES = test-rhythmdb-tree-deserialization.c
test_rhythmdb_tree_deserialization_LDADD =				\
	$(top_builddir)/rhythmdb/librhythmdb.la			\
	$(top_builddir)/lib/librb.la				\
	$(RHYTHMBOX_LIBS)

noinst_LTLIBRARIES = librhythmdb.la

librhythmdb_la_SOURCES =				\
	rhythmdb.h					\
	rhythmdb.c

if USE_TREEDB
librhythmdb_la_SOURCES += rhythmdb-tree.h rhythmdb-tree.c
endif

librhythmdb_la_LDFLAGS = -export-dynamic

INCLUDES = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"RhythmDB\"			\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	$(RHYTHMBOX_CFLAGS)

TESTS = $(noinst_PROGRAMS)

EXTRA_DIST = deserialization-test1.xml
