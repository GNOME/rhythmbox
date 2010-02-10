noinst_LTLIBRARIES = librbwidgets.la

librbwidgets_la_SOURCES =				\
	rb-entry-view.c					\
	rb-entry-view.h					\
	rb-property-view.c				\
	rb-property-view.h				\
	rb-search-entry.c				\
	rb-search-entry.h				\
	rb-header.c					\
	rb-header.h					\
	rb-dialog.c					\
	rb-dialog.h					\
	rb-cell-renderer-pixbuf.c			\
	rb-cell-renderer-pixbuf.h			\
	rb-cell-renderer-rating.c			\
	rb-cell-renderer-rating.h			\
	gossip-cell-renderer-expander.c			\
	gossip-cell-renderer-expander.h			\
	gedit-message-area.c				\
	gedit-message-area.h				\
	rb-song-info.c					\
	rb-song-info.h					\
	rb-rating.c					\
	rb-rating.h					\
	rb-rating-helper.c				\
	rb-segmented-bar.c				\
	rb-segmented-bar.h				\
	rb-library-browser.h				\
	rb-library-browser.c				\
	rb-rating-helper.h				\
	rb-query-creator.c				\
	rb-query-creator.h				\
	rb-query-creator-private.h			\
	rb-query-creator-properties.c			\
	rb-uri-dialog.c					\
	rb-uri-dialog.h

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
	-I$(top_srcdir)/shell				\
	-I$(top_srcdir)/backends			\
	$(NOTIFY_CFLAGS)				\
	$(RHYTHMBOX_CFLAGS)

librbwidgets_la_LDFLAGS = -export-dynamic
librbwidgets_la_LIBADD = $(NOTIFY_LIBS)

noinst_PROGRAMS = test-rb-segmented-bar
test_rb_segmented_bar_SOURCES = test-rb-segmented-bar.c
test_rb_segmented_bar_LDADD = librbwidgets.la $(RHYTHMBOX_LIBS)
