noinst_LTLIBRARIES = librbview.la

librbview_la_SOURCES =					\
	rb-view-player.c				\
	rb-view-player.h				\
	rb-view-status.c				\
	rb-view-status.h				\
	rb-view-clipboard.h				\
	rb-view-clipboard.c				\
	rb-view.h					\
	rb-view.c

INCLUDES =						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	-I$(top_srcdir)/lib/widgets			\
	-DDATADIR=\""$(datadir)"\"			\
	$(WARN_CFLAGS)					\
	$(RHYTHMBOX_CFLAGS)

librbview_la_LDFLAGS = -export-dynamic
