noinst_LTLIBRARIES = librbxine-output.la

MARSHALFILES = gtkxine-marshal.h gtkxine-marshal.c
GLIB_GENMARSHAL = `pkg-config --variable=glib_genmarshal glib-2.0`

gtkxine-marshal.h: gtkxine-marshal.list
	( $(GLIB_GENMARSHAL) --prefix=gtkxine_marshal gtkxine-marshal.list \
	  --header > gtkxine-marshal.tmp \
	  && mv gtkxine-marshal.tmp gtkxine-marshal.h ) \
	|| (rm -f gtkxine-marshal.tmp && exit 1 )
gtkxine-marshal.c: gtkxine-marshal.h
	( $(GLIB_GENMARSHAL) --prefix=gtkxine_marshal gtkxine-marshal.list \
	  --header --body > gtkxine-marshal.tmp \
	  && mv gtkxine-marshal.tmp gtkxine-marshal.c ) \
	|| (rm -f gtkxine-marshal.tmp && exit 1 )

librbxine_output_la_SOURCES =				\
	$(MARSHALFILES)					\
	debug.h						\
	gtk-xine.c					\
	gtk-xine.h					\
	scrsaver.c					\
	scrsaver.h					\
	video-utils.c					\
	video-utils.h

INCLUDES =						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	$(WARN_CFLAGS)					\
	$(RHYTHMBOX_CFLAGS)

librbxine_output_la_LDFLAGS = -export-dynamic

CLEANFILES = $(MARSHALFILES)

libdir = ${exec_prefix}/lib/xine/plugins
lib_LTLIBRARIES = xineplug_inp_gnomevfs.la

xineplug_inp_gnomevfs_la_SOURCES = input_gnomevfs.c
xineplug_inp_gnomevfs_la_LIBADD = $(VFS_LIBS)
xineplug_inp_gnomevfs_la_LDFLAGS = -avoid-version -module
