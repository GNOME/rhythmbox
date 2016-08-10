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
	$(test_utils)

test_audioscrobbler_LDADD = \
	$(LDADD) \
	$(top_builddir)/plugins/audioscrobbler/libaudioscrobblertest.la \
	$(RHYTHMBOX_LIBS)

test_widgets_SOURCES = \
	test-widgets.c						\
	test-widgets-resources.c				\
	$(test_utils)

test_resource_deps = $(shell $(GLIB_COMPILE_RESOURCES) --generate-dependencies --sourcedir $(top_srcdir)/data $(srcdir)/test-widgets.gresource.xml)
test-widgets-resources.c: $(test_resource_deps) $(srcdir)/test-widgets.gresource.xml
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES)			\
		--target="$@"					\
		--sourcedir="$(top_srcdir)/data"		\
		--generate-source "$(srcdir)/test-widgets.gresource.xml"

bench_rhythmdb_load_SOURCES = bench-rhythmdb-load.c

AM_CPPFLAGS = 							\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"	        \
	-DG_LOG_DOMAIN=\"Rhythmbox-tests\"			\
	-I$(top_srcdir) 					\
	$(RHYTHMBOX_CFLAGS)					\
	-I$(top_srcdir)/lib					\
	-I$(top_srcdir)/metadata				\
	-I$(top_srcdir)/widgets					\
	-I$(top_srcdir)/rhythmdb				\
	-I$(top_srcdir)/podcast					\
	-I$(top_srcdir)/plugins/audioscrobbler

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
	test-widgets.gresource.xml				\
	$(OLD_TESTS)

CLEANFILES = test-widgets-resources.c
