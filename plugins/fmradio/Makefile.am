
plugindir = $(PLUGINDIR)/fmradio
plugindatadir = $(PLUGINDATADIR)/fmradio
plugin_LTLIBRARIES = libfmradio.la

libfmradio_la_SOURCES = \
  fmradio-resources.c \
  rb-fm-radio-plugin.c \
  rb-fm-radio-source.c \
  rb-fm-radio-source.h \
  rb-fm-radio-gst-src.c \
  rb-radio-tuner.h \
  rb-radio-tuner-v4l2.c

libfmradio_la_LDFLAGS = $(PLUGIN_LIBTOOL_FLAGS)
libfmradio_la_LIBTOOLFLAGS = --tag=disable-static
libfmradio_la_LIBADD = $(top_builddir)/shell/librhythmbox-core.la

AM_CPPFLAGS = \
  -DGNOMELOCALEDIR=\""$(datadir)/locale"\" \
  -DG_LOG_DOMAIN=\"Rhythmbox\" \
  -I$(top_srcdir) \
  -I$(top_srcdir)/lib \
  -I$(top_srcdir)/metadata \
  -I$(top_srcdir)/rhythmdb \
  -I$(top_srcdir)/widgets \
  -I$(top_srcdir)/sources \
  -I$(top_srcdir)/plugins \
  -I$(top_srcdir)/shell \
  -I$(top_srcdir)/backends \
  -DPIXMAP_DIR=\""$(datadir)/pixmaps"\" \
  -DSHARE_DIR=\"$(pkgdatadir)\" \
  -DDATADIR=\""$(datadir)"\" \
  $(RHYTHMBOX_CFLAGS) \
  $(TOTEM_PLPARSER_CFLAGS) \
  $(WARN_CFLAGS)

plugin_in_files = fmradio.plugin.in
%.plugin: %.plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

PLUGIN_RESOURCES = fmradio-toolbar.ui fmradio-popup.ui

plugin_resource_deps = $(shell $(GLIB_COMPILE_RESOURCES) --generate-dependencies --sourcedir $(srcdir) $(srcdir)/fmradio.gresource.xml)
fmradio-resources.c: $(plugin_resource_deps) $(srcdir)/fmradio.gresource.xml
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES)		\
		--target="$@"				\
		--sourcedir="$(srcdir)"			\
		--sourcedir="$(builddir)"		\
		--generate-source "$(srcdir)/fmradio.gresource.xml"

BUILT_SOURCES = fmradio-resources.c

plugin_DATA = $(plugin_in_files:.plugin.in=.plugin)

EXTRA_DIST = $(PLUGIN_RESOURCES) $(plugin_in_files) fmradio.gresource.xml

CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)
