noinst_LTLIBRARIES = librbwidgets.la

librbwidgets_la_SOURCES =				\
	rb-sidebar.c					\
	rb-sidebar.h					\
	rb-sidebar-private.h				\
	rb-sidebar-button.c				\
	rb-sidebar-button.h				\
	rb-tree-model-node.c				\
	rb-tree-model-node.h				\
	rb-node-view.c					\
	rb-node-view.h					\
	rb-search-entry.c				\
	rb-search-entry.h				\
	rb-link.c					\
	rb-link.h					\
	rb-player.c					\
	rb-player.h					\
	rb-volume.c					\
	rb-volume.h					\
	eggtreemodelfilter.c				\
	eggtreemodelfilter.h				\
	eggtreemultidnd.c				\
	eggtreemultidnd.h				\
	rb-ellipsizing-label.c				\
	rb-ellipsizing-label.h				\
	rb-cell-renderer-pixbuf.c			\
	rb-cell-renderer-pixbuf.h			\
	rb-cell-renderer-rating.c			\
	rb-cell-renderer-rating.h			\
	rb-song-info.c					\
	rb-song-info.h					\
	rb-tree-model-sort.c				\
	rb-tree-model-sort.h				\
	gtktreeprivate.h				\
	gtkrbtree.h					\
	rb-rating.c					\
	rb-rating.h

INCLUDES =						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	-I$(top_srcdir)/lib/view			\
	-I$(top_srcdir)/library				\
	$(RHYTHMBOX_CFLAGS)

librbwidgets_la_LDFLAGS = -export-dynamic
librbwidgets_la_LIDADD = $(top_builddir)/lib/view/librbview.la
