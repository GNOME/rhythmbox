## arch-tag: Automake rules for misc. utility functions

noinst_LTLIBRARIES = librb.la

librb_la_SOURCES =					\
	rb-atomic.h					\
	rb-atomic.c					\
	rb-debug.c					\
	rb-debug.h					\
	rb-dialog.c					\
	rb-dialog.h					\
	rb-file-helpers.c				\
	rb-file-helpers.h				\
	rb-file-monitor.c				\
	rb-file-monitor.h				\
	rb-marshal.c					\
	rb-marshal.h					\
	disclosure-widget.c				\
	disclosure-widget.h				\
	gsequence.c					\
	gsequence.h					\
	rb-bonobo-helpers.c				\
	rb-bonobo-helpers.h				\
	rb-glade-helpers.c				\
	rb-glade-helpers.h				\
	rb-string-helpers.c				\
	rb-string-helpers.h				\
	rb-stock-icons.c				\
	rb-stock-icons.h				\
	rb-cut-and-paste-code.c				\
	rb-cut-and-paste-code.h				\
	rb-thread-helpers.c				\
	rb-thread-helpers.h				\
	rb-util.c					\
	rb-util.h					\
	rb-playlist.c					\
	rb-playlist.h					\
	rb-enums.h					\
	eel-gconf-extensions.c				\
	eel-gconf-extensions.h				\
	rb-tree-dnd.c					\
	rb-tree-dnd.h					\
	$(EGGFILES)					\
	rb-preferences.h	

INCLUDES =						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-DSHARE_DIR=\"$(pkgdatadir)\"			\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/library				\
	-I$(top_srcdir)/rhythmdb			\
	$(WARN_CFLAGS)					\
	$(RHYTHMBOX_CFLAGS)

EGGFILES=						\
	eggtrayicon.h					\
	eggtrayicon.c
EGGDIR=$(srcdir)/../../libegg/libegg/tray/
regenerate-built-sources:
	EGGFILES="$(EGGFILES)" EGGDIR="$(EGGDIR)" $(srcdir)/update-from-egg.sh || true

BUILT_SOURCES=rb-marshal.c rb-marshal.h

CLEAN_FILES = $(BUILT_SOURCES)

rb-marshal.c: rb-marshal.list
	@GLIB_GENMARSHAL@ --prefix=rb_marshal $(srcdir)/rb-marshal.list --header --body > rb-marshal.c

rb-marshal.h: rb-marshal.list
	@GLIB_GENMARSHAL@ --prefix=rb_marshal $(srcdir)/rb-marshal.list --header > rb-marshal.h

librb_la_LDFLAGS = -export-dynamic

EXTRA_DIST = rb-marshal.list
