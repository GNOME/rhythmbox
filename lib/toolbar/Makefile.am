noinst_LTLIBRARIES = libgultoolbar.la

libgultoolbar_la_SOURCES =				\
	gul-gobject-misc.h				\
	gul-tbi-separator.c				\
	gul-tbi-separator.h				\
	gul-toolbar-bonobo-view.c			\
	gul-toolbar-bonobo-view.h			\
	gul-toolbar-editor.c				\
	gul-toolbar-editor.h				\
	gul-toolbar-item-factory.c			\
	gul-toolbar-item-factory.h			\
	gul-tbi-std-toolitem.c				\
	gul-tbi-std-toolitem.h				\
	gul-toolbar.c					\
	gul-toolbar.h 					\
	gul-toolbar-item.c				\
	gul-toolbar-item.h				\
	gul-toolbar-tree-model.c			\
	gul-toolbar-tree-model.h			\
	gul-tbi-volume.c				\
	gul-tbi-volume.h

INCLUDES =						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-DSHARE_DIR=\"$(pkgdatadir)\"			\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib				\
	$(RHYTHMBOX_CFLAGS)

libgultoolbar_la_LDFLAGS = -export-dynamic
