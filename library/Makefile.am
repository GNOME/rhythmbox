noinst_LTLIBRARIES = librblibrary.la

librblibrary_la_SOURCES =				\
	rb-library.c					\
	rb-library.h					\
	rb-library-walker-thread.c			\
	rb-library-walker-thread.h			\
	rb-library-main-thread.c			\
	rb-library-main-thread.h			\
	rb-library-action-queue.c			\
	rb-library-action-queue.h			\
	rb-library-action.c				\
	rb-library-action.h				\
	rb-node.c					\
	rb-node.h					\
	rb-node-song.c					\
	rb-node-song.h					\
	rb-node-filter.c				\
	rb-node-filter.h				\
	rb-library-dnd-types.h

INCLUDES =						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	$(RHYTHMBOX_CFLAGS)

librblibrary_la_LDFLAGS = -export-dynamic
librblibrary_la_LIBADD = $(top_builddir)/lib/librb.la
