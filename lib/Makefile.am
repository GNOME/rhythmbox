SUBDIRS = view widgets

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
	rb-bonobo-helpers.c				\
	rb-bonobo-helpers.h				\
	rb-glade-helpers.c				\
	rb-glade-helpers.h				\
	rb-string-helpers.c				\
	rb-string-helpers.h				\
	rb-stock-icons.c				\
	rb-stock-icons.h				\
	eel-gconf-extensions.c				\
	eel-gconf-extensions.h

INCLUDES =						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-DSHARE_DIR=\"$(pkgdatadir)\"			\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/library				\
	$(RHYTHMBOX_CFLAGS)

librb_la_LDFLAGS = -export-dynamic
librb_la_LIBADD = $(top_builddir)/lib/widgets/librbwidgets.la \
		  $(top_builddir)/lib/view/librbview.la
