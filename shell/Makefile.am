bin_PROGRAMS = rhythmbox 

rhythmbox_interface_idl_sources =      		\
	Rhythmbox-common.c	     			\
	Rhythmbox-stubs.c	     			\
	Rhythmbox-skels.c	     			\
	Rhythmbox.h

rhythmbox_SOURCES =              			\
	$(rhythmbox_interface_idl_sources) 		\
	main.c						\
	rb-shell.c					\
	rb-shell.h					\
	rb-shell-player.c				\
	rb-shell-player.h				\
	rb-source-header.c				\
	rb-source-header.h				\
	rb-playlist.c					\
	rb-playlist.h					\
	rb-shell-preferences.c				\
	rb-shell-preferences.h				\
	rb-remote.c					\
	rb-remote.h

rhythmbox_LDADD =					\
	$(top_builddir)/lib/librb.la	                \
	$(top_builddir)/sources/libsources.la	        \
	$(top_builddir)/sources/libsourcesimpl.la	\
	$(top_builddir)/library/librblibrary.la	        \
	$(top_builddir)/iradio/librbiradio.la	        \
	$(RHYTHMBOX_LIBS)

rhythmbox_LDFLAGS = -export-dynamic

INCLUDES = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib                        	\
	-I$(top_srcdir)/lib/widgets                    	\
	-I$(top_srcdir)/lib/toolbar                    	\
	-I$(top_srcdir)/sources                    	\
	-I$(top_srcdir)/iradio                    	\
	-I$(top_srcdir)/library                    	\
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	$(WARN_CFLAGS)					\
	$(RHYTHMBOX_CFLAGS)				\
	-D_XOPEN_SOURCE

CLEAN_FILES = $(rhythmbox_interface_idl_sources)

Rhythmbox-common.c Rhythmbox-stubs.c Rhythmbox-skels.c Rhythmbox.h: Rhythmbox.idl
	$(ORBIT_IDL) -I $(LIBBONOBO_IDL) -I $(BONOBO_ACTIVATION_IDL) $(srcdir)/Rhythmbox.idl

EXTRA_DIST = Rhythmbox.idl

CLEANFILES =			\
	Rhythmbox-common.c	\
	Rhythmbox-stubs.c	\
	Rhythmbox-skels.c	\
	Rhythmbox.h

