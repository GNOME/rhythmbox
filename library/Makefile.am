noinst_LTLIBRARIES = librblibrary.la

librblibrary_la_SOURCES =				\
	rb-library.c					\
	rb-library.h					\
	rb-library-watcher-thread.c			\
	rb-library-watcher-thread.h			\
	rb-library-main-thread.c			\
	rb-library-main-thread.h			\
	rb-library-xml-thread.c				\
	rb-library-xml-thread.h				\
	rb-library-action-queue.c			\
	rb-library-action-queue.h			\
	rb-library-preferences.h			\
	rb-node.c					\
	rb-node.h					\
	rb-node-song.c					\
	rb-node-song.h					\
	rb-node-iterator.c				\
	rb-node-iterator.h

INCLUDES =						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	$(RHYTHMBOX_CFLAGS)

librblibrary_la_LDFLAGS = -export-dynamic
librblibrary_la_LIBADD = $(top_builddir)/lib/librb.la
