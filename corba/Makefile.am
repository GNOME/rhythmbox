## arch-tag: Automake rules for Rhythmbox corba interface

#lib_LTLIBRARIES = librbcorba.la
noinst_LTLIBRARIES = librbcorba.la

nodist_librbcorba_la_SOURCES = $(rhythmbox_interface_idl_sources)

rhythmbox_interface_idl_sources =      	\
	Rhythmbox-common.c	     			\
	Rhythmbox-stubs.c	     			\
	Rhythmbox-skels.c	     			\
	Rhythmbox.h

INCLUDES = 							\
	$(WARN_CFLAGS)					\
	$(RHYTHMBOX_CFLAGS)				\
	-D_XOPEN_SOURCE

idldir = $(prefix)/share/idl
idl_DATA = Rhythmbox.idl

CLEANFILES = $(rhythmbox_interface_idl_sources)

$(rhythmbox_interface_idl_sources): Rhythmbox.idl
	$(ORBIT_IDL) -I $(LIBBONOBO_IDL) -I $(BONOBO_ACTIVATION_IDL) Rhythmbox.idl

BUILT_SOURCES = $(rhythmbox_interface_idl_sources)

EXTRA_DIST = Rhythmbox.idl

