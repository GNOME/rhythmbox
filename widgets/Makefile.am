## arch-tag: Automake rules for Rhythmbox's collection of various widgets

noinst_LTLIBRARIES = librbwidgets.la

librbwidgets_la_SOURCES =				\
	rb-song-display-box.c				\
	rb-song-display-box.h				\
	rb-entry-view.c					\
	rb-entry-view.h					\
	rb-property-view.c				\
	rb-property-view.h				\
	rb-search-entry.c				\
	rb-search-entry.h				\
	rb-player.c					\
	rb-player.h					\
	rb-volume.c					\
	rb-volume.h					\
	rb-druid.c					\
	rb-druid.h					\
	$(EGGFILES)					\
	rb-ellipsizing-label.c				\
	rb-ellipsizing-label.h				\
	rb-cell-renderer-pixbuf.c			\
	rb-cell-renderer-pixbuf.h			\
	rb-cell-renderer-rating.c			\
	rb-cell-renderer-rating.h			\
	rb-song-info.c					\
	rb-song-info.h					\
	rb-load-failure-dialog.c			\
	rb-load-failure-dialog.h			\
	gtktreeprivate.h				\
	gtkrbtree.h					\
	rb-rating.c					\
	rb-rating.h					\
	rb-tree-view.c					\
	rb-tree-view.h					\
	rb-query-creator.c				\
	rb-query-creator.h				\
	rb-tree-view-column.c				\
	rb-tree-view-column.h

INCLUDES =						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	-I$(top_builddir)/lib 				\
	-I$(top_srcdir)/rhythmdb			\
	-I$(top_srcdir)/sources				\
	-I$(top_srcdir)/library				\
	-I$(top_srcdir)/iradio				\
	-I$(top_srcdir)/monkey-media			\
	-I$(top_builddir)/monkey-media			\
	$(WARN_CFLAGS)					\
	$(RHYTHMBOX_CFLAGS)

librbwidgets_la_LDFLAGS = -export-dynamic
librbwidgets_la_LIBADD = $(top_builddir)/monkey-media/libmonkey-media.la $(top_builddir)/lib/librb.la $(top_builddir)/rhythmdb/librhythmdb.la 
