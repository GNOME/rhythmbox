noinst_LTLIBRARIES = libviews.la

libviews_la_SOURCES =					\
	rb-library-view.c				\
	rb-library-view.h				\
	rb-view-cmd.c					\
	rb-view-cmd.h					\
        rb-group-view.c                                 \
        rb-group-view.h					\
	rb-song-info-helpers.c				\
	rb-song-info-helpers.h				\
	rb-audiocd-view.c				\
	rb-audiocd-view.h

INCLUDES =						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-DSHARE_DIR=\"$(pkgdatadir)\"			\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	-I$(top_srcdir)/lib/view 			\
	-I$(top_srcdir)/lib/widgets 			\
	-I$(top_srcdir)/library 			\
	$(RHYTHMBOX_CFLAGS)

libviews_la_LDFLAGS = -export-dynamic
