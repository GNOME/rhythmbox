## arch-tag: Automake rules for internet radio library

noinst_LTLIBRARIES = librbiradio.la

librbiradio_la_SOURCES =				\
	rb-new-station-dialog.c				\
	rb-new-station-dialog.h				\
	rb-station-properties-dialog.c			\
	rb-station-properties-dialog.h


INCLUDES =						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	-I$(top_srcdir)/rhythmdb			\
	-I$(top_srcdir)/widgets				\
	-I$(top_srcdir)/library				\
	-I$(top_srcdir)/monkey-media			\
	-I$(top_builddir)/monkey-media			\
	$(RHYTHMBOX_CFLAGS)

librbiradio_la_LDFLAGS = -export-dynamic
librbiradio_la_LIBADD = $(top_builddir)/lib/librb.la $(top_builddir)/monkey-media/libmonkey-media.la
