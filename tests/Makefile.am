noinst_LTLIBRARIES = libtests.la

libtests_la_SOURCES =					\
	testview.c					\
	testview.h					\
	testview2.c					\
	testview2.h

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

libtests_la_LDFLAGS = -export-dynamic
