## arch-tag: Automake rules for test programs
## Process this file with automake to produce Makefile.in

noinst_PROGRAMS = test-info test-cmdline

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
	-I$(top_srcdir)/rhythmdb
