noinst_LTLIBRARIES = librbiradio.la

librbiradio_la_SOURCES =				\
	rb-new-station-dialog.c				\
	rb-new-station-dialog.h				\
	rb-station-properties-dialog.c			\
	rb-station-properties-dialog.h			\
	rb-iradio-backend.c				\
	rb-iradio-backend.h				\
	rb-iradio-station.c				\
	rb-iradio-station.h				\
	rb-iradio-yp-iterator.c				\
	rb-iradio-yp-iterator.h				\
	rb-iradio-yp-xmlfile.c				\
	rb-iradio-yp-xmlfile.h				\
	rb-node-station.c				\
	rb-node-station.h				


INCLUDES =						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	-I$(top_srcdir)/lib/widgets			\
	-I$(top_srcdir)/library				\
	$(RHYTHMBOX_CFLAGS)

librbiradio_la_LDFLAGS = -export-dynamic
librbiradio_la_LIBADD = $(top_builddir)/lib/librb.la
