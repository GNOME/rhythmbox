noinst_LTLIBRARIES = librbwidgets.la

librbwidgets_la_SOURCES =				\
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
	rb-druid.c					\
	rb-druid.h					\
	rb-sourcelist.c					\
	rb-sourcelist.h					\
	rb-sourcelist-model.c				\
	rb-sourcelist-model.h				\
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
	rb-load-failure-dialog.c			\
	rb-load-failure-dialog.h			\
	rb-tree-model-sort.c				\
	rb-tree-model-sort.h				\
	gtktreeprivate.h				\
	gtkrbtree.h					\
	rb-rating.c					\
	rb-rating.h					\
	rb-tree-view.c					\
	rb-tree-view.h					\
	rb-tree-view-column.c				\
	rb-tree-view-column.h


if GTK_TREE_MODEL_SORT_WORKAROUND
librbwidgets_la_SOURCES += gtktreemodelsort.c
librbwidgets_la_SOURCES += gtktreemodelsort.h
librbwidgets_la_SOURCES += gtktreedatalist.c
librbwidgets_la_SOURCES += gtktreedatalist.h
librbwidgets_la_SOURCES += gtkintl.h
endif


INCLUDES =						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	-I$(top_srcdir)/sources				\
	-I$(top_srcdir)/library				\
	-I$(top_srcdir)/iradio				\
	-I$(top_builddir)/lib 				\
	-I$(top_srcdir)/monkey-media			\
	-I$(top_builddir)/monkey-media			\
	$(WARN_CFLAGS)					\
	$(RHYTHMBOX_CFLAGS)

librbwidgets_la_LDFLAGS = -export-dynamic
librbwidgets_la_LIDADD = $(top_builddir)/sources/libsources.la  $(top_builddir)/monkey-media/libmonkey-media.la 
