noinst_LTLIBRARIES = libsexy.la

libsexy_la_SOURCES =		\
	sexy-icon-entry.h	\
	sexy-icon-entry.c

libsexy_la_LIBADD = $(RHYTHMBOX_LIBS)

libsexy_la_LDFLAGS = -export-dynamic

INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"			\
	$(RHYTHMBOX_CFLAGS)

if WITH_INTERNAL_LIBSEXY
INCLUDES += -I$(top_srcdir)/widgets
endif

