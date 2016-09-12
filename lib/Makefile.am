
SUBDIRS = libmediaplayerid

noinst_LTLIBRARIES = librb.la

rbincludedir = $(includedir)/rhythmbox/lib
rbinclude_HEADERS =					\
	rb-builder-helpers.h				\
	rb-debug.h					\
	rb-file-helpers.h				\
	rb-list-model.h					\
	rb-stock-icons.h				\
	rb-string-value-map.h				\
	rb-util.h					\
	rb-task-progress.h				\
	rb-task-progress-simple.h

librb_la_SOURCES =					\
	$(rbinclude_HEADERS)				\
	rb-debug.c					\
	rb-file-helpers.c				\
	rb-builder-helpers.c				\
	rb-stock-icons.c				\
	rb-cut-and-paste-code.c				\
	rb-cut-and-paste-code.h				\
	rb-util.c					\
	rb-tree-dnd.c					\
	rb-tree-dnd.h					\
	rb-string-value-map.c				\
	rb-async-queue-watch.c				\
	rb-async-queue-watch.h				\
	rb-text-helpers.c				\
	rb-text-helpers.h				\
	rb-gst-media-types.c				\
	rb-gst-media-types.h				\
	rb-missing-plugins.c				\
	rb-missing-plugins.h				\
	rb-async-copy.c					\
	rb-async-copy.h					\
	rb-chunk-loader.c				\
	rb-chunk-loader.h				\
	rb-task-progress.c				\
	rb-task-progress-simple.c			\
	rb-list-model.c

AM_CPPFLAGS =						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-DSHARE_DIR=\"$(pkgdatadir)\"			\
	-DDATADIR=\""$(datadir)"\"			\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/widgets				\
	$(RHYTHMBOX_CFLAGS)

librb_la_LDFLAGS = -export-dynamic
