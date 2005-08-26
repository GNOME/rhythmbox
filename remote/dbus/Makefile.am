## arch-tag: Automake rules for Rhythmbox DBus interface

noinst_LTLIBRARIES = librbdbus.la


INCLUDES = 						\
	$(WARN_CFLAGS)					\
	-DDATADIR=\"$(datadir)\"			\
	$(RHYTHMBOX_CFLAGS)				\
	$(DBUS_CFLAGS)					\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	-I$(top_srcdir)/lib				\
	-I$(top_srcdir)/remote				\
	-I$(top_srcdir)/remote/dbus			\
	-I$(top_builddir)/remote/dbus			\
	-D_XOPEN_SOURCE

librbdbus_la_SOURCES = rb-remote-dbus.c rb-remote-dbus.h
librbdbus_la_LIBADD =					\
	$(RHYTHMBOX_LIBS) 				\
	$(DBUS_LIBS)

