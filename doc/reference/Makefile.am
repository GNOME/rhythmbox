AUTOMAKE_OPTIONS = 1.6

DOC_MODULE=rhythmbox
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.sgml
DOC_SOURCE_DIR= ../../

# Extra options to supply to gtkdoc-mkdb.
MKDB_OPTIONS=--sgml-mode --output-format=xml

# Extra doc dir eg. --extra-dir=../html
FIXXREF_OPTIONS=

# Used for dependencies. The docs will be rebuilt if any of these change.
HFILE_GLOB=$(top_srcdir)/lib/*.h
CFILE_GLOB=$(top_srcdir)/lib/*.c

# Header files to ignore when scanning.
IGNORE_HFILES= \
	config.h \
	itunesdb.h \
	rb-cut-and-paste-code.h \
	rb-commander.h \
	rb-marshal.h \
	rb-nautilus-context-menu.h \
	rb-recorder-marshal.h \
	rb-remote-bonobo.h \
	rb-remote-client-proxy.h \
	rb-remote-common.h \
	rb-remote-dbus.h \
	rb-remote.h \
	rb-remote-proxy.h \
	Rhythmbox.h \
	rhythmdb-gda.h \
	rhythmdb-marshal.h \
	rhythmdb-private.h

INCLUDES=\
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-I$(top_srcdir)/sources \
	-I$(top_srcdir)/shell \
	-I$(top_srcdir)/rhythmdb \
	-I$(top_srcdir)/player \
	-I$(top_srcdir)/metadata \
	-I$(top_srcdir)/widgets \
	-I$(top_srcdir)/lib \
	-I$(top_srcdir)/iradio \
	-I$(top_srcdir)/remote \
	-I$(top_srcdir)/daapsharing \
	$(DBUS_CFLAGS) \
	$(RHYTHMBOX_CFLAGS)
	
GTKDOC_LIBS=\
	$(top_builddir)/shell/librbshell.la	        \
	$(top_builddir)/sources/libsources.la	        \
	$(top_builddir)/sources/libsourcesimpl.la	\
	$(top_builddir)/rhythmdb/librhythmdb.la         \
	$(top_builddir)/iradio/librbiradio.la	        \
	$(top_builddir)/remote/librbremote.la		\
	$(top_builddir)/player/librbplayer.la		\
	$(top_builddir)/metadata/librbmetadata.la	\
	$(top_builddir)/widgets/librbwidgets.la         \
	$(top_builddir)/lib/librb.la	                \
	$(top_builddir)/daapsharing/libdaapsharing.la	\
	$(TOTEM_PLPARSER_LIBS)				\
	$(HAL_LIBS)					\
	$(LIBNAUTILUS_BURN_LIBS)			\
	$(DBUS_LIBS)					\
	$(SOUP_LIBS)					\
	$(MDNS_LIBS)					\
	$(RHYTHMBOX_LIBS)

content_files =
HTML_IMAGES =

include $(top_srcdir)/gtk-doc.make

