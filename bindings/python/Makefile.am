## Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES = rb.la

nodist_rb_la_SOURCES = rhythmdb.c rb.c

rb_la_SOURCES = override_common.c override_common.h

rb_la_LDFLAGS = \
	-module -avoid-version

rb_la_LIBADD = \
	$(PYTHON_LIBS)		\
	$(PYTHON_EXTRA_LIBS)	\
	$(PYGTK_LIBS)

rb_la_CFLAGS = \
	-I$(top_srcdir)			\
	-I$(top_srcdir)/lib             \
	-I$(top_srcdir)/lib/libmediaplayerid \
	-I$(top_srcdir)/metadata	\
	-I$(top_srcdir)/rhythmdb	\
	-I$(top_srcdir)/widgets		\
	-I$(top_srcdir)/sources		\
	-I$(top_srcdir)/podcast		\
	-I$(top_srcdir)/plugins		\
	-I$(top_srcdir)/backends	\
	-I$(top_srcdir)/shell		\
	$(RHYTHMBOX_CFLAGS)		\
	$(PYGTK_CFLAGS)			\
	$(PYTHON_CFLAGS)		\
	$(AM_CFLAGS)			\
	$(WNOERROR_CFLAGS)

rhythmdb.c: rhythmdb.defs rhythmdb.override
	( cd $(srcdir) && $(PYGOBJECT_CODEGEN) \
		--register $(PYGTK_DEFSDIR)/gtk-types.defs \
		--register $(GST_PYTHON_DEFSDIR)/gst-types.defs \
		--override $*.override \
		--prefix py$* $(<F) ) > $@

rb.c: rb.defs rb.override
	( cd $(srcdir) && $(PYGOBJECT_CODEGEN) \
		--register $(PYGTK_DEFSDIR)/gtk-types.defs \
		--register $(PYGTK_DEFSDIR)/gdk-base-types.defs \
		--register $(GST_PYTHON_DEFSDIR)/gst-types.defs \
		--register rhythmdb.defs \
		--override $*.override \
		--prefix py$* $(<F) ) > $@

RHYTHMDB_BINDING_HEADERS_SRCDIR_IN = \
	rhythmdb/rhythmdb.h			\
	rhythmdb/rhythmdb-entry.h		\
	rhythmdb/rhythmdb-entry-type.h		\
	rhythmdb/rhythmdb-property-model.h	\
	rhythmdb/rhythmdb-query-model.h		\
	rhythmdb/rhythmdb-query-results.h	\
	rhythmdb/rhythmdb-import-job.h		\
	$(NULL)

RB_BINDING_HEADERS_SRCDIR_IN = \
	backends/rb-player.h			\
	backends/gstreamer/rb-player-gst.h	\
	lib/rb-string-value-map.h		\
	lib/rb-cut-and-paste-code.h		\
	lib/rb-file-helpers.h			\
	metadata/rb-metadata.h			\
	shell/rb-plugin.h			\
	shell/rb-removable-media-manager.h	\
	shell/rb-shell.h			\
	shell/rb-shell-player.h			\
	shell/rb-shell-preferences.h		\
	shell/rb-track-transfer-batch.h		\
	shell/rb-track-transfer-queue.h		\
	sources/rb-source.h			\
	sources/rb-auto-playlist-source.h	\
	sources/rb-browser-source.h		\
	sources/rb-playlist-source.h		\
	sources/rb-removable-media-source.h	\
	sources/rb-sourcelist.h			\
	sources/rb-sourcelist-model.h		\
	sources/rb-static-playlist-source.h	\
	sources/rb-streaming-source.h		\
	sources/rb-source-search.h		\
	sources/rb-source-search-basic.h	\
	widgets/rb-entry-view.h			\
	widgets/rb-library-browser.h		\
	widgets/rb-property-view.h		\
	widgets/rb-search-entry.h		\
	widgets/rb-song-info.h			\
	widgets/rb-uri-dialog.h			\
	$(NULL)

RB_BINDING_HEADERS_BUILDDIR_IN =
RHYTHMDB_BINDING_HEADERS_BUILDDIR_IN =

RB_BINDING_HEADERS_SRCDIR		:= $(addprefix $(top_srcdir)/,$(RB_BINDING_HEADERS_SRCDIR_IN))
RB_BINDING_HEADERS_BUILDDIR		:= $(addprefix $(top_builddir)/,$(RB_BINDING_HEADERS_BUILDDIR_IN))
RHYTHMDB_BINDING_HEADERS_SRCDIR		:= $(addprefix $(top_srcdir)/,$(RHYTHMDB_BINDING_HEADERS_SRCDIR_IN))
RHYTHMDB_BINDING_HEADERS_BUILDDIR	:= $(addprefix $(top_builddir)/,$(RHYTHMDB_BINDING_HEADERS_BUILDDIR_IN))

regenerate-python-binding:
	$(PYGOBJECT_H2DEF) -n RB -m rb $(sort $(RB_BINDING_HEADERS_SRCDIR) $(RB_BINDING_HEADERS_BUILDDIR)) > rb.defs.new
	$(PYGOBJECT_H2DEF) -n RhythmDB -m rhythmdb $(sort $(RHYTHMDB_BINDING_HEADERS_SRCDIR) $(RHYTHMDB_BINDING_HEADERS_BUILDDIR)) > rhythmdb.defs.new

BUILT_SOURCES = rhythmdb.c rb.c

EXTRA_DIST = \
	rb.override		\
	rb.defs			\
	rhythmdb.override	\
	rhythmdb.defs

CLEANFILES = $(BUILT_SOURCES)

dist-hook:
	cd $(distdir); rm -f $(BUILT_SOURCES)
