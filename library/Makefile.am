noinst_LTLIBRARIES = librblibrary.la

librblibrary_la_SOURCES =				\
	rb-library.c					\
	rb-library.h					\
	rb-library-private.c				\
	rb-library-private.h				\
	rb-library-thread.c				\
	rb-library-thread.h				\
	rb-library-watcher.c				\
	rb-library-watcher.h				\
	rb-library-preferences.h

INCLUDES =						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	$(RHYTHMBOX_CFLAGS)

librblibrary_la_LDFLAGS = -export-dynamic
librblibrary_la_LIBADD = $(top_srcdir)/lib/librb.la
