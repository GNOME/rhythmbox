bin_PROGRAMS = rhythmbox 

rhythmbox_shell_interface_idl_sources =      		\
	RhythmboxShell-common.c	     			\
	RhythmboxShell-stubs.c	     			\
	RhythmboxShell-skels.c	     			\
	RhythmboxShell.h

rhythmbox_SOURCES =              			\
	$(rhythmbox_shell_interface_idl_sources) 	\
	main.c						\
	rb-shell.c					\
	rb-shell.h					\
	rb-shell-player.c				\
	rb-shell-player.h				\
	rb-shell-status.c				\
	rb-shell-status.h				\
	rb-shell-clipboard.c				\
	rb-shell-clipboard.h				\
	rb-shell-preferences.c				\
	rb-shell-preferences.h				\
	rb-remote.c					\
	rb-remote.h

rhythmbox_LDADD =					\
	$(top_builddir)/lib/librb.la	                \
	$(top_builddir)/views/libviews.la	        \
	$(top_builddir)/library/librblibrary.la	        \
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
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	$(RHYTHMBOX_CFLAGS)

CLEAN_FILES = $(rhythmbox_shell_interface_idl_sources)

RhythmboxShell-common.c RhythmboxShell-stubs.c RhythmboxShell-skels.c RhythmboxShell.h: RhythmboxShell.idl
	$(ORBIT_IDL) -I $(LIBBONOBO_IDL) -I $(BONOBO_ACTIVATION_IDL) $(srcdir)/RhythmboxShell.idl

EXTRA_DIST = RhythmboxShell.idl

CLEANFILES =			\
	RhythmboxShell-common.c	\
	RhythmboxShell-stubs.c	\
	RhythmboxShell-skels.c	\
	RhythmboxShell.h

