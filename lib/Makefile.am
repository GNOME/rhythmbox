SUBDIRS = widgets

noinst_LTLIBRARIES = librb.la

librb_la_SOURCES =					\
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
	rb-bonobo-helpers.c				\
	rb-bonobo-helpers.h				\
	rb-glade-helpers.c				\
	rb-glade-helpers.h				\
	rb-glist-wrapper.c				\
	rb-glist-wrapper.h				\
	rb-string-helpers.c				\
	rb-string-helpers.h				\
	rb-stock-icons.c				\
	rb-stock-icons.h				\
	rb-cut-and-paste-code.c				\
	rb-cut-and-paste-code.h				\
	rb-thread-helpers.c				\
	rb-thread-helpers.h				\
# 	rb-windows-ini-file.c				\
# 	rb-windows-ini-file.h				\
	rb-enums.h					\
	eel-gconf-extensions.c				\
	eel-gconf-extensions.h				\
# 	getline.c					\
# 	getline.h					\
# 	getstr.c					\
# 	getstr.h					\
	eggtrayicon.c					\
	eggtrayicon.h					\
	rb-preferences.h	

INCLUDES =						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-DSHARE_DIR=\"$(pkgdatadir)\"			\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/library				\
	$(WARN_CFLAGS)					\
	$(RHYTHMBOX_CFLAGS)

BUILT_SOURCES=rb-marshal.c rb-marshal.h

CLEAN_FILES = $(BUILT_SOURCES)

rb-marshal.c: rb-marshal.list
	@GLIB_GENMARSHAL@ --prefix=rb_marshal $(srcdir)/rb-marshal.list --header --body > rb-marshal.c

rb-marshal.h: rb-marshal.list
	@GLIB_GENMARSHAL@ --prefix=rb_marshal $(srcdir)/rb-marshal.list --header > rb-marshal.h

librb_la_LDFLAGS = -export-dynamic
librb_la_LIBADD = $(top_builddir)/lib/widgets/librbwidgets.la

EXTRA_DIST = rb-marshal.list
