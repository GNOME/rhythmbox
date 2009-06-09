
plugindir = $(PLUGINDIR)/fmradio
plugin_LTLIBRARIES = libfmradio.la

libfmradio_la_SOURCES = \
  rb-fm-radio-plugin.c \
  rb-fm-radio-source.c \
  rb-fm-radio-source.h \
  rb-fm-radio-gst-src.c \
  rb-radio-tuner.h \
  rb-radio-tuner-v4l2.c

libfmradio_la_LDFLAGS = $(PLUGIN_LIBTOOL_FLAGS)
libfmradio_la_LIBTOOLFLAGS = --tag=disable-static
libfmradio_la_LIBADD = $(top_builddir)/shell/librhythmbox-core.la

INCLUDES = \
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
  $(WARN_CFLAGS) \
  -D_XOPEN_SOURCE -D_BSD_SOURCE

plugin_in_files = fmradio.rb-plugin.in
%.rb-plugin: %.rb-plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

uixmldir = $(plugindir)
uixml_DATA = fmradio-ui.xml

plugin_DATA = $(plugin_in_files:.rb-plugin.in=.rb-plugin)

EXTRA_DIST = $(uixml_DATA) $(plugin_in_files)

CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)

