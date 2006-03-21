## arch-tag: Automake rules for misc. utility functions

noinst_LTLIBRARIES = librb.la

if MKDTEMP_MISSING
MKDTEMP_FILES = mkdtemp.c mkdtemp.h
else
MKDTEMP_FILES =
endif

librb_la_SOURCES =					\
	$(MKDTEMP_FILES)				\
	rb-debug.c					\
	rb-debug.h					\
	rb-file-helpers.c				\
	rb-file-helpers.h				\
	rb-glade-helpers.c				\
	rb-glade-helpers.h				\
	rb-stock-icons.c				\
	rb-stock-icons.h				\
	rb-cut-and-paste-code.c				\
	rb-cut-and-paste-code.h				\
	rb-util.c					\
	rb-util.h					\
	rb-enums.h					\
	rb-tree-dnd.c					\
	rb-tree-dnd.h					\
	rb-preferences.h				\
	md5.c						\
	md5.h						\
	rb-proxy-config.c				\
	rb-proxy-config.h

INCLUDES =						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-DSHARE_DIR=\"$(pkgdatadir)\"			\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/widgets				\
	$(WARN_CFLAGS)					\
	$(SOUP_CFLAGS)					\
	$(RHYTHMBOX_CFLAGS)

BUILT_SOURCES=

CLEAN_FILES = $(BUILT_SOURCES)

rb-marshal.c: rb-marshal.list
	@GLIB_GENMARSHAL@ --prefix=rb_marshal $(srcdir)/rb-marshal.list --header --body > rb-marshal.c

rb-marshal.h: rb-marshal.list
	@GLIB_GENMARSHAL@ --prefix=rb_marshal $(srcdir)/rb-marshal.list --header > rb-marshal.h
BUILT_SOURCES += rb-marshal.c rb-marshal.h

librb_la_SOURCES += $(BUILT_SOURCES)

librb_la_LDFLAGS = -export-dynamic

EXTRA_DIST = rb-marshal.list
