## arch-tag: Automake rules for remote interfaces

if WITH_BONOBO
MAYBE_BONOBO = bonobo
else
MAYBE_BONOBO =
endif

#if WITH_DBUS
#MAYBE_DBUS = dbus
#else
#MAYBE_DBUS =
#endif

SUBDIRS = $(MAYBE_BONOBO) # $(MAYBE_DBUS)

noinst_LTLIBRARIES = librbremote.la

librbremote_la_SOURCES = rb-remote-common.h \
			 rb-remote-common.c \
			 rb-remote-proxy.h \
			 rb-remote-proxy.c \
			 rb-remote-client-proxy.h \
			 rb-remote-client-proxy.c

INCLUDES =						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib 				\
	$(WARN_CFLAGS)					\
	$(RHYTHMBOX_CFLAGS)

librbremote_la_LDFLAGS = -export-dynamic
