## arch-tag: Automake rules for test programs
## Process this file with automake to produce Makefile.in

#TESTS = test-rhythmdb-simple test-rhythmdb-indexing \
#		  test-rhythmdb-query test-rhythmdb-tree-serialization \
#		  test-rhythmdb-tree-deserialization \
#		  test-rhythmdb-view

TESTS = 

noinst_PROGRAMS = test-info test-cmdline $(TESTS)

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
test_rhythmdb_tree_deserialization_LDADD =			\
	$(top_builddir)/rhythmdb/librhythmdb.la			\
	$(top_builddir)/lib/librb.la				\
	$(RHYTHMBOX_LIBS)

test_rhythmdb_view_SOURCES = test-rhythmdb-view.c
test_rhythmdb_view_LDADD =					\
	$(top_builddir)/rhythmdb/librhythmdb.la			\
	$(top_builddir)/lib/librb.la				\
	$(top_builddir)/widgets/librbwidgets.la			\
	$(RHYTHMBOX_LIBS)

test_info_SOURCES = test-info.c

test_info_LDADD =						\
	$(top_builddir)/monkey-media/libmonkey-media.la		\
	$(RHYTHMBOX_LIBS)

test_info_LDFLAGS = -export-dynamic

test_cmdline_SOURCES = test-cmdline.c

test_cmdline_LDADD =						\
	$(top_builddir)/monkey-media/libmonkey-media.la		\
	$(RHYTHMBOX_LIBS)

test_cmdline_LDFLAGS = -export-dynamic

if HAVE_AUDIOCD
noinst_PROGRAMS += test-cd

test_cd_SOURCES = test-cd.c

test_cd_LDADD = 						\
	$(top_builddir)/monkey-media/libmonkey-media.la			\
	$(RHYTHMBOX_LIBS)

test_cd_LDFLAGS = -export-dynamic
endif

INCLUDES = 							\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"	        \
	-DG_LOG_DOMAIN=\"Rhythmbox-tests\"			\
	-I$(top_srcdir) 					\
	$(RHYTHMBOX_CFLAGS)					\
	-I$(top_srcdir)/lib					\
	-I$(top_srcdir)/monkey-media				\
	-I$(top_srcdir)/rhythmdb				\
	-I$(top_srcdir)/widgets


EXTRA_DIST = deserialization-test1.xml deserialization-test2.xml deserialization-test3.xml
