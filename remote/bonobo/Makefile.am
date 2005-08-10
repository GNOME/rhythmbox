## arch-tag: Automake rules for Rhythmbox corba interface

noinst_LTLIBRARIES = librbcorba.la librbbonobo.la

noinst_PROGRAMS = test-corba
test_corba_SOURCES = test-corba.c 
test_corba_LDADD = librbcorba.la $(RHYTHMBOX_LIBS) $(ORBIT_LIBS)

nodist_librbcorba_la_SOURCES = $(rhythmbox_interface_idl_sources)

rhythmbox_interface_idl_sources =      	\
	Rhythmbox-common.c	     			\
	Rhythmbox-stubs.c	     			\
	Rhythmbox-skels.c	     			\
	Rhythmbox.h

INCLUDES = 						\
	$(WARN_CFLAGS)					\
	-DDATADIR=\"$(datadir)\"			\
	$(RHYTHMBOX_CFLAGS)				\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	-I$(top_srcdir)/lib				\
	-I$(top_srcdir)/remote				\
	-I$(top_srcdir)/remote/bonobo			\
	-I$(top_builddir)/remote			\
	-I$(top_builddir)/remote/bonobo			\
	-D_XOPEN_SOURCE

nodist_noinst_HEADERS = Rhythmbox.h

idldir = $(prefix)/share/idl
idl_DATA = Rhythmbox.idl

$(rhythmbox_interface_idl_sources): $(srcdir)/Rhythmbox.idl
	$(ORBIT_IDL) -I $(LIBBONOBO_IDL) -I $(BONOBO_ACTIVATION_IDL) $(srcdir)/Rhythmbox.idl

BUILT_SOURCES = $(rhythmbox_interface_idl_sources)

bonobodir = $(libdir)/bonobo
bonobo_LTLIBRARIES = librb-nautilus-context-menu.la

librb_nautilus_context_menu_la_SOURCES =			\
	rb-nautilus-context-menu.c			\
	rb-nautilus-context-menu.h			\
	libmain.c

rb-nautilus-context-menu: Rhythmbox.h

librb_nautilus_context_menu_la_LIBADD = librbcorba.la $(RHYTHMBOX_LIBS)
librb_nautilus_context_menu_la_LDFLAGS = -module -avoid-version

server_in_files = Rhythmbox_Nautilus_Context_Menu.server.in.in

serverdir = $(libdir)/bonobo/servers
server_DATA = $(server_in_files:.server.in.in=.server)
$(server_in_files:.server.in.in=.server.in): $(server_in_files)
	sed -e "s|\@BONOBODIR\@|$(bonobodir)|" $< > $@

@INTLTOOL_SERVER_RULE@

librbbonobo_la_SOURCES = rb-remote-bonobo.c rb-remote-bonobo.h
librbbonobo_la_LIBADD = 				\
	librbcorba.la 					\
	$(RHYTHMBOX_LIBS) 				\
	$(ORBIT_LIBS) 					\
	$(BONOBO_LIBS)

EXTRA_DIST = $(server_in_files) Rhythmbox.idl

CLEANFILES = $(rhythmbox_interface_idl_sources) $(server_DATA) $(server_in_files:.server.in.in=.server.in)

## idl compiler generates an unused variable at the top of Rhythmbox-common.c,
## which breaks -Werror on Mac OSX.
AM_CFLAGS = -Wno-error
