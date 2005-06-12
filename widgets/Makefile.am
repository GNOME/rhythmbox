## arch-tag: Automake rules for Rhythmbox's collection of various widgets

noinst_LTLIBRARIES = librbwidgets.la

librbwidgets_la_SOURCES =				\
	eel-gconf-extensions.c				\
	eel-gconf-extensions.h				\
	rb-song-display-box.c				\
	rb-song-display-box.h				\
	rb-entry-view.c					\
	rb-entry-view.h					\
	rb-property-view.c				\
	rb-property-view.h				\
	rb-search-entry.c				\
	rb-search-entry.h				\
	rb-header.c					\
	rb-header.h					\
	rb-volume.c					\
	rb-volume.h					\
	rb-druid.c					\
	rb-druid.h					\
	disclosure-widget.c				\
	disclosure-widget.h				\
	eggtrayicon.c					\
	eggtrayicon.h					\
	rb-dialog.c					\
	rb-dialog.h					\
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
	rb-rating-helper.c				\
	rb-rating-helper.h				\
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
	-I$(top_srcdir)/player				\
	-I$(top_srcdir)/metadata			\
	-I$(top_srcdir)/rhythmdb			\
	-I$(top_srcdir)/sources				\
	-I$(top_srcdir)/library				\
	-I$(top_srcdir)/iradio				\
	$(WARN_CFLAGS)					\
	$(RHYTHMBOX_CFLAGS)

librbwidgets_la_LDFLAGS = -export-dynamic
