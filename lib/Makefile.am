## arch-tag: Automake rules for misc. utility functions

noinst_LTLIBRARIES = librb.la

librb_la_SOURCES =					\
	rb-debug.c					\
	rb-debug.h					\
	rb-file-helpers.c				\
	rb-file-helpers.h				\
	rb-glade-helpers.c				\
	rb-glade-helpers.h				\
	rb-string-helpers.c				\
	rb-string-helpers.h				\
	rb-stock-icons.c				\
	rb-stock-icons.h				\
	rb-cut-and-paste-code.c				\
	rb-cut-and-paste-code.h				\
	rb-util.c					\
	rb-util.h					\
	totem-pl-parser.c				\
	totem-pl-parser.h				\
	rb-enums.h					\
	rb-tree-dnd.c					\
	rb-tree-dnd.h					\
	rb-preferences.h	

INCLUDES =						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-DSHARE_DIR=\"$(pkgdatadir)\"			\
	-I$(top_srcdir) 				\
	$(WARN_CFLAGS)					\
	$(RHYTHMBOX_CFLAGS)

BUILT_SOURCES=

CLEAN_FILES = $(BUILT_SOURCES)

rb-marshal.c: rb-marshal.list
	@GLIB_GENMARSHAL@ --prefix=rb_marshal $(srcdir)/rb-marshal.list --header --body > rb-marshal.c

rb-marshal.h: rb-marshal.list
	@GLIB_GENMARSHAL@ --prefix=rb_marshal $(srcdir)/rb-marshal.list --header > rb-marshal.h
BUILT_SOURCES += rb-marshal.c rb-marshal.h

totemplparser-marshal.c: totemplparser-marshal.h
	@GLIB_GENMARSHAL@ --prefix=totemplparser_marshal $(srcdir)/totemplparser-marshal.list --header --body > totemplparser-marshal.c
totemplparser-marshal.h: totemplparser-marshal.list
	@GLIB_GENMARSHAL@ --prefix=totemplparser_marshal $(srcdir)/totemplparser-marshal.list --header > totemplparser-marshal.h

BUILT_SOURCES += totemplparser-marshal.c totemplparser-marshal.h

librb_la_SOURCES += $(BUILT_SOURCES)

librb_la_LDFLAGS = -export-dynamic

EXTRA_DIST = rb-marshal.list
