SUBDIRS = view widgets

noinst_LTLIBRARIES = librb.la

librb_la_SOURCES =					\
	rb-debug.c					\
	rb-debug.h					\
	rb-dialog.c					\
	rb-dialog.h					\
	rb-file-helpers.c				\
	rb-file-helpers.h				\
	rb-bonobo-helpers.c				\
	rb-bonobo-helpers.h				\
	rb-stock-icons.c				\
	rb-stock-icons.h				\
	rb-stream.c					\
	rb-stream.h					\
	e-splash.c					\
	e-splash.h					\
	eel-gconf-extensions.c				\
	eel-gconf-extensions.h				\
	rb-node.c					\
	rb-node.h					\
	rb-node-search.c				\
	rb-node-search.h

INCLUDES =						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-DSHARE_DIR=\"$(pkgdatadir)\"			\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/library				\
	$(RHYTHMBOX_CFLAGS)

librb_la_LDFLAGS = -export-dynamic
librb_la_LIBADD = widgets/librbwidgets.la \
		  view/librbview.la
