## arch-tag: Automake rules for podcast library

noinst_LTLIBRARIES = librbpodcast.la

librbpodcast_la_SOURCES =				\
	rb-new-podcast-dialog.c				\
	rb-new-podcast-dialog.h				\
	rb-feed-podcast-properties-dialog.c		\
	rb-feed-podcast-properties-dialog.h		\
	rb-podcast-properties-dialog.c			\
	rb-podcast-properties-dialog.h			\
	rb-podcast-parse.c				\
	rb-podcast-parse.h				\
	rb-podcast-manager.c				\
	rb-podcast-manager.h


INCLUDES =						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	-I$(top_srcdir)/rhythmdb			\
	-I$(top_srcdir)/widgets				\
	-I$(top_srcdir)/shell				\
	-I$(top_srcdir)/metadata			\
	-I$(top_srcdir)/library				\
	-I$(top_builddir)/lib 				\
	$(RHYTHMBOX_CFLAGS)

librbpodcast_la_LDFLAGS = -export-dynamic

