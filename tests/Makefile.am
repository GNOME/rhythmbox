## Process this file with automake to produce Makefile.in

TESTS = 

LDADD = \
	$(CHECK_LIBS)						\
	$(top_builddir)/rhythmdb/librhythmdb.la 		\
	$(top_builddir)/metadata/librbmetadata.la		\
	$(top_builddir)/widgets/librbwidgets.la			\
	$(top_builddir)/podcast/librbpodcast.la			\
	$(top_builddir)/lib/librb.la				\
	$(RHYTHMBOX_LIBS)

test_utils = \
	test-utils.h						\
	test-utils.c

test_rhythmdb_SOURCES = \
	test-rhythmdb.c						\
	$(test_utils)

test_rhythmdb_query_model_SOURCES = \
	test-rhythmdb-query-model.c				\
	$(test_utils)

test_rhythmdb_property_model_SOURCES = \
	test-rhythmdb-property-model.c				\
	$(test_utils)

test_file_helpers_SOURCES = \
	test-file-helpers.c					\
	$(test_utils)

test_rb_lib_SOURCES = \
	test-rb-lib.c						\
	$(test_utils)

test_audioscrobbler_SOURCES = \
	test-audioscrobbler.c								\
	$(top_srcdir)/plugins/audioscrobbler/rb-audioscrobbler-entry.c			\
	$(top_srcdir)/plugins/audioscrobbler/rb-audioscrobbler-radio-track-entry-type.c	\
	$(test_utils)

test_widgets_SOURCES = \
	test-widgets.c						\
	$(test_utils)

bench_rhythmdb_load_SOURCES = bench-rhythmdb-load.c

INCLUDES = 							\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"	        \
	-DG_LOG_DOMAIN=\"Rhythmbox-tests\"			\
	-I$(top_srcdir) 					\
	$(RHYTHMBOX_CFLAGS)					\
	-I$(top_srcdir)/lib					\
	-I$(top_srcdir)/metadata				\
	-I$(top_srcdir)/widgets					\
	-I$(top_srcdir)/rhythmdb				\
	-I$(top_srcdir)/podcast					\
	-I$(top_srcdir)/plugins/audioscrobbler			\
	-D_XOPEN_SOURCE -D_BSD_SOURCE

if HAVE_CHECK
TESTS += \
	test-rb-lib						\
	test-rhythmdb						\
	test-rhythmdb-query-model				\
	test-rhythmdb-property-model				\
	test-file-helpers					\
	test-audioscrobbler					\
	test-widgets
endif

OLD_TESTS = \
	test-rhythmdb-query.c					\
	test-rhythmdb-tree-serialization.c			\
	test-rhythmdb-view.c

noinst_PROGRAMS = \
		bench-rhythmdb-load				\
		$(TESTS)


EXTRA_DIST = 							\
	deserialization-test1.xml 				\
	deserialization-test2.xml 				\
	deserialization-test3.xml 				\
	podcast-upgrade.xml					\
	$(OLD_TESTS)
