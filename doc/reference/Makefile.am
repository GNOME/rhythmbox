AUTOMAKE_OPTIONS = 1.6

DOC_MODULE=rhythmbox
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.sgml
DOC_SOURCE_DIR= ../../


# Extra options to pass to gtkdoc-scangobj. Not normally needed.
SCANGOBJ_OPTIONS=

# Extra options to supply to gtkdoc-scan.
# e.g. SCAN_OPTIONS=--deprecated-guards="GTK_DISABLE_DEPRECATED"
SCAN_OPTIONS=

# Extra options to supply to gtkdoc-mkdb.
MKDB_OPTIONS=--sgml-mode --output-format=xml

# Extra options to supply to gtkdoc-mktmpl
# e.g. MKTMPL_OPTIONS=--only-section-tmpl
MKTMPL_OPTIONS=

# Extra options to supply to gtkdoc-fixref. Not normally needed.
# e.g. FIXXREF_OPTIONS=--extra-dir=../gdk-pixbuf/html --extra-dir=../gdk/html
FIXXREF_OPTIONS=

# Used for dependencies. The docs will be rebuilt if any of these change.
HFILE_GLOB=$(top_srcdir)/lib/*.h
CFILE_GLOB=$(top_srcdir)/lib/*.c

# Header files to ignore when scanning.
IGNORE_HFILES= \
	config.h \
	gsequence.h \
	md5.h \
	mkdtemp.h \
	rb-cut-and-paste-code.h \
	rb-marshal.h \
	rhythmdb-private.h \
	sexy-icon-entry.h \
	\
	rb-shell-glue.h \
	rb-shell-binding.h \
	rb-shell-player-glue.h \
	rb-shell-player-binding.h \
	rb-playlist-manager-glue.h


# Images to copy into HTML directory.
# e.g. HTML_IMAGES=$(top_srcdir)/gtk/stock-icons/stock_about_24.png
HTML_IMAGES=

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
# e.g. content_files=running.sgml building.sgml changes-2.0.sgml
content_files=

# SGML files where gtk-doc abbrevations (#GtkWidget) are expanded
# These files must be listed here *and* in content_files
# e.g. expand_content_files=running.sgml
expand_content_files=



INCLUDES=\
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-I$(top_builddir)/lib \
	-I$(top_srcdir)/sources \
	-I$(top_srcdir)/shell \
	-I$(top_srcdir)/rhythmdb \
	-I$(top_srcdir)/metadata \
	-I$(top_srcdir)/widgets \
	-I$(top_srcdir)/lib \
	-I$(top_srcdir)/plugins \
	-I$(top_srcdir)/backends \
	$(DBUS_CFLAGS) \
	$(IPOD_CFLAGS) \
	$(SOUP_CFLAGS) \
	$(RHYTHMBOX_CFLAGS)
	
if ENABLE_PYTHON
BINDING_LIBS = $(top_builddir)/bindings/python/rb.la
MORE_GTKDOC_LIBS = $(PYTHON_LIBS)
endif

GTKDOC_LIBS=\
	$(top_builddir)/shell/librhythmbox-core.la        \
	$(BINDING_LIBS)					\
	$(TOTEM_PLPARSER_LIBS)				\
	$(HAL_LIBS)					\
	$(LIBNAUTILUS_BURN_LIBS)			\
	$(DBUS_LIBS)					\
	$(SOUP_LIBS)					\
	$(MDNS_LIBS)					\
	$(IPOD_LIBS)					\
	$(NOTIFY_LIBS)					\
	$(RHYTHMBOX_LIBS)				\
	$(MORE_GTKDOC_LIBS)

include $(top_srcdir)/gtk-doc.make

