
AUTHORS.tab : $(top_srcdir)/AUTHORS
	cat $(top_srcdir)/AUTHORS | sed -e 's/^/"/' -e 's/$$/",/' > $@.tmp
	mv $@.tmp $@

MAINTAINERS.tab : $(top_srcdir)/MAINTAINERS
	cat $(top_srcdir)/MAINTAINERS | sed -e 's/^/"/' -e 's/$$/",/' > $@.tmp
	mv $@.tmp $@

DOCUMENTERS.tab : $(top_srcdir)/DOCUMENTERS
	cat $(top_srcdir)/DOCUMENTERS | sed -e 's/^/"/' -e 's/$$/",/' > $@.tmp
	mv $@.tmp $@

tab_files = AUTHORS.tab MAINTAINERS.tab DOCUMENTERS.tab

bin_PROGRAMS = rhythmbox 

rhythmbox_SOURCES =              			\
	main.c						\
	rb-shell.c					\
	rb-shell.h					\
	rb-shell-player.c				\
	rb-shell-player.h				\
	rb-source-header.c				\
	rb-source-header.h				\
	rb-statusbar.c					\
	rb-statusbar.h					\
	rb-playlist.c					\
	rb-playlist.h					\
	rb-shell-preferences.c				\
	rb-shell-preferences.h				\
	rb-shell-clipboard.c				\
	rb-shell-clipboard.h				\
	rb-remote.c					\
	rb-remote.h

rhythmbox_LDADD =					\
	$(top_builddir)/lib/librb.la	                \
	$(top_builddir)/sources/libsources.la	        \
	$(top_builddir)/sources/libsourcesimpl.la	\
	$(top_builddir)/library/librblibrary.la	        \
	$(top_builddir)/iradio/librbiradio.la	        \
	$(top_builddir)/corba/librbcorba.la	        \
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
	-I$(top_srcdir)/corba                    	\
	-I$(top_srcdir)/iradio                    	\
	-I$(top_srcdir)/library                    	\
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	$(WARN_CFLAGS)					\
	$(RHYTHMBOX_CFLAGS)				\
	-D_XOPEN_SOURCE

CLEAN_FILES = $(rhythmbox_interface_idl_sources)

BUILT_SOURCES = $(tab_files)

CLEANFILES = AUTHORS.tab MAINTAINERS.tab
