bin_PROGRAMS = rhythmbox 

rhythmbox_interface_idl_sources =      		\
	Rhythmbox-common.c	     			\
	Rhythmbox-stubs.c	     			\
	Rhythmbox-skels.c	     			\
	Rhythmbox.h

rhythmbox_SOURCES =              			\
	$(rhythmbox_interface_idl_sources) 		\
	main.c						\
	rb.c						\
	rb.h						\
	rb-player.c					\
	rb-player.h					\
	rb-commander.c					\
	rb-commander.h	

rhythmbox_LDADD =					\
	$(top_builddir)/lib/librb.la	                \
	$(top_builddir)/views/libviews.la	        \
	$(top_builddir)/library/librblibrary.la	        \
	$(top_builddir)/xine-output/librbxine-output.la \
	$(RHYTHMBOX_LIBS)

rhythmbox_LDFLAGS = -export-dynamic

INCLUDES = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib                        	\
	-I$(top_srcdir)/lib/widgets                    	\
	-I$(top_srcdir)/lib/view                    	\
	-I$(top_srcdir)/lib/toolbar                    	\
	-I$(top_srcdir)/views                    	\
	-I$(top_srcdir)/library                    	\
	-I$(top_srcdir)/xine-output			\
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	$(WARN_CFLAGS)					\
	$(RHYTHMBOX_CFLAGS)

CLEAN_FILES = $(rhythmbox_interface_idl_sources)

Rhythmbox-common.c Rhythmbox-stubs.c Rhythmbox-skels.c Rhythmbox.h: Rhythmbox.idl
	$(ORBIT_IDL) -I $(LIBBONOBO_IDL) -I $(BONOBO_ACTIVATION_IDL) $(srcdir)/Rhythmbox.idl

EXTRA_DIST = Rhythmbox.idl

CLEANFILES =			\
	Rhythmbox-common.c	\
	Rhythmbox-stubs.c	\
	Rhythmbox-skels.c	\
	Rhythmbox.h

