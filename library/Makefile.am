## arch-tag: Automake rules for Rhythmbox song library

noinst_LTLIBRARIES = librblibrary.la

librblibrary_la_SOURCES =				\
	rb-library.c					\
	rb-library.h					\
	rb-library-main-thread.c			\
	rb-library-main-thread.h			\
	rb-library-action.c				\
	rb-library-action.h

INCLUDES =						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	-I$(top_builddir)/lib 				\
	-I$(top_srcdir)/rhythmdb			\
	-I$(top_srcdir)/monkey-media			\
	-I$(top_builddir)/monkey-media			\
	$(WARN_CFLAGS)					\
	$(RHYTHMBOX_CFLAGS)

librblibrary_la_LDFLAGS = -export-dynamic
librblibrary_la_LIBADD = $(top_builddir)/lib/librb.la
