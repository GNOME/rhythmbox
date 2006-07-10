## arch-tag: Automake rules for test programs
## Process this file with automake to produce Makefile.in

TESTS = 

test_rhythmdb_SOURCES = test-rhythmdb.c
test_rhythmdb_LDADD = \
	$(CHECK_LIBS)						\
	$(top_builddir)/rhythmdb/librhythmdb.la 		\
	$(top_builddir)/lib/librb.la				\
	$(top_builddir)/metadata/librbmetadata.la		\
	$(top_builddir)/widgets/librbwidgets.la			\
	$(RHYTHMBOX_LIBS)

bench_rhythmdb_load_SOURCES = bench-rhythmdb-load.c
bench_rhythmdb_load_LDADD = \
	$(CHECK_LIBS)						\
	$(top_builddir)/rhythmdb/librhythmdb.la 		\
	$(top_builddir)/lib/librb.la				\
	$(top_builddir)/metadata/librbmetadata.la		\
	$(top_builddir)/widgets/librbwidgets.la			\
	$(RHYTHMBOX_LIBS)

INCLUDES = 							\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"	        \
	-DG_LOG_DOMAIN=\"Rhythmbox-tests\"			\
	-I$(top_srcdir) 					\
	$(RHYTHMBOX_CFLAGS)					\
	-I$(top_srcdir)/lib					\
	-I$(top_srcdir)/metadata				\
	-I$(top_srcdir)/widgets					\
	-I$(top_srcdir)/rhythmdb


if HAVE_CHECK
TESTS += test-rhythmdb
endif

OLD_TESTS = \
	test-rhythmdb-query.c					\
	test-rhythmdb-tree-serialization.c			\
	test-rhythmdb-view.c

noinst_PROGRAMS = \
		bench-rhythmdb-load				\
		$(TESTS)


EXTRA_DIST = deserialization-test1.xml deserialization-test2.xml deserialization-test3.xml $(OLD_TESTS)
