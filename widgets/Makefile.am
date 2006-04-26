## arch-tag: Automake rules for Rhythmbox's collection of various widgets

noinst_LTLIBRARIES = librbwidgets.la

if WITH_INTERNAL_LIBSEXY
SUBDIRS = libsexy
LIBSEXY_LIBS += $(top_builddir)/widgets/libsexy/libsexy.la
endif

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
	bacon-volume.c					\
	bacon-volume.h					\
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
	rb-rating.c					\
	rb-rating.h					\
	rb-rating-helper.c				\
	rb-library-browser.h				\
	rb-library-browser.c				\
	rb-rating-helper.h				\
	rb-query-creator.c				\
	rb-query-creator.h				\
	rb-query-creator-private.h			\
	rb-query-creator-properties.c

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
	$(WARN_CFLAGS)					\
	$(NOTIFY_CFLAGS)				\
	$(LIBSEXY_CFLAGS)				\
	$(RHYTHMBOX_CFLAGS)

librbwidgets_la_LDFLAGS = -export-dynamic
librbwidgets_la_LIBADD = $(LIBSEXY_LIBS)

