noinst_LTLIBRARIES = librbwidgets.la

widgetincludedir = $(includedir)/rhythmbox/widgets
widgetinclude_HEADERS =					\
	rb-alert-dialog.h				\
	rb-entry-view.h					\
	rb-property-view.h				\
	rb-dialog.h					\
	rb-cell-renderer-pixbuf.h			\
	rb-cell-renderer-rating.h			\
	rb-rating.h					\
	rb-library-browser.h				\
	rb-segmented-bar.h				\
	rb-song-info.h					\
	rb-source-toolbar.h				\
	rb-uri-dialog.h					\
	rb-fading-image.h				\
	rb-object-property-editor.h			\
	rb-import-dialog.h				\
	rb-button-bar.h					\
	rb-task-list-display.h				\
	rb-encoding-settings.h				\
	nautilus-floating-bar.h

librbwidgets_la_SOURCES =				\
	$(widgetinclude_HEADERS)			\
	rb-alert-dialog.c				\
	rb-entry-view.c					\
	rb-property-view.c				\
	rb-search-entry.c				\
	rb-search-entry.h				\
	rb-header.c					\
	rb-header.h					\
	rb-dialog.c					\
	rb-cell-renderer-pixbuf.c			\
	rb-cell-renderer-rating.c			\
	gossip-cell-renderer-expander.c			\
	gossip-cell-renderer-expander.h			\
	rb-song-info.c					\
	rb-rating.c					\
	rb-rating-helper.c				\
	rb-segmented-bar.c				\
	rb-library-browser.c				\
	rb-rating-helper.h				\
	rb-query-creator.c				\
	rb-query-creator.h				\
	rb-query-creator-private.h			\
	rb-query-creator-properties.c			\
	rb-uri-dialog.c					\
	eggwrapbox.c					\
	eggwrapbox.h					\
	eggwrapbox-enums.c				\
	eggwrapbox-enums.h				\
	rb-source-toolbar.c				\
	rb-fading-image.c				\
	rb-object-property-editor.c			\
	rb-import-dialog.c				\
	rb-button-bar.c					\
	rb-task-list-display.c				\
	rb-encoding-settings.c				\
	nautilus-floating-bar.c

AM_CPPFLAGS =						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	-I$(top_builddir)/lib 				\
	-I$(top_srcdir)/metadata			\
	-I$(top_srcdir)/rhythmdb			\
	-I$(top_srcdir)/sources				\
	-I$(top_srcdir)/shell				\
	-I$(top_srcdir)/backends			\
	$(NOTIFY_CFLAGS)				\
	$(RHYTHMBOX_CFLAGS)

librbwidgets_la_LDFLAGS = -export-dynamic
librbwidgets_la_LIBADD = $(NOTIFY_LIBS)

noinst_PROGRAMS = test-rb-segmented-bar test-uri-dialog
test_rb_segmented_bar_SOURCES = test-rb-segmented-bar.c
test_rb_segmented_bar_LDADD = librbwidgets.la $(RHYTHMBOX_LIBS)
test_uri_dialog_SOURCES = test-uri-dialog.c
test_uri_dialog_LDADD = librbwidgets.la $(top_builddir)/lib/librb.la $(RHYTHMBOX_LIBS)
