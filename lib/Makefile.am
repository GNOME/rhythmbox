## arch-tag: Automake rules for misc. utility functions

noinst_LTLIBRARIES = librb.la

if MKDTEMP_MISSING
MKDTEMP_FILES = mkdtemp.c mkdtemp.h
else
MKDTEMP_FILES =
endif

librb_la_SOURCES =					\
	$(MKDTEMP_FILES)				\
	rb-debug.c					\
	rb-debug.h					\
	eel-gconf-extensions.c				\
	eel-gconf-extensions.h				\
	rb-file-helpers.c				\
	rb-file-helpers.h				\
	rb-glade-helpers.c				\
	rb-glade-helpers.h				\
	rb-stock-icons.c				\
	rb-stock-icons.h				\
	rb-cut-and-paste-code.c				\
	rb-cut-and-paste-code.h				\
	rb-util.c					\
	rb-util.h					\
	rb-tree-dnd.c					\
	rb-tree-dnd.h					\
	rb-preferences.h				\
	md5.c						\
	md5.h						\
	rb-proxy-config.c				\
	rb-proxy-config.h				\
	rb-string-value-map.c				\
	rb-string-value-map.h

if WITH_INTERNAL_LIBSEXY
librb_la_SOURCES += gsequence.c gsequence.h
endif

INCLUDES =						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-DSHARE_DIR=\"$(pkgdatadir)\"			\
	-DDATADIR=\""$(datadir)"\"			\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/widgets				\
	$(WARN_CFLAGS)					\
	$(SOUP_CFLAGS)					\
	$(RHYTHMBOX_CFLAGS)

BUILT_SOURCES=

CLEAN_FILES = $(BUILT_SOURCES)

rb-marshal.c: rb-marshal.list
	@GLIB_GENMARSHAL@ --prefix=rb_marshal $(srcdir)/rb-marshal.list --header --body > rb-marshal.c

rb-marshal.h: rb-marshal.list
	@GLIB_GENMARSHAL@ --prefix=rb_marshal $(srcdir)/rb-marshal.list --header > rb-marshal.h

# inline icons
rhythmbox-set-star-inline.h: $(top_srcdir)/data/art/rhythmbox-set-star.png
	@GDK_PIXBUF_CSOURCE@ --raw --name=rhythmbox_set_star_inline $< > $@

rhythmbox-unset-star-inline.h: $(top_srcdir)/data/art/rhythmbox-unset-star.png
	@GDK_PIXBUF_CSOURCE@ --raw --name=rhythmbox_unset_star_inline $< > $@

rhythmbox-no-star-inline.h: $(top_srcdir)/data/art/rhythmbox-no-star.png
	@GDK_PIXBUF_CSOURCE@ --raw --name=rhythmbox_no_star_inline $< > $@

rhythmbox-podcast-inline.h: $(top_srcdir)/data/art/rhythmbox-podcast.png
	@GDK_PIXBUF_CSOURCE@ --raw --name=rhythmbox_podcast_inline $< > $@

rhythmbox-tray-icon-inline.h: $(top_srcdir)/data/art/rhythmbox-tray-icon.png
	@GDK_PIXBUF_CSOURCE@ --raw --name=rhythmbox_tray_icon_inline $< > $@

media-eject-inline.h: $(top_srcdir)/data/art/media-eject.png
	@GDK_PIXBUF_CSOURCE@ --raw --name=media_eject_inline $< > $@


BUILT_SOURCES += 				\
	rb-marshal.c 				\
	rb-marshal.h 				\
	rhythmbox-set-star-inline.h		\
	rhythmbox-unset-star-inline.h		\
	rhythmbox-no-star-inline.h		\
	rhythmbox-podcast-inline.h		\
	rhythmbox-tray-icon-inline.h		\
	media-eject-inline.h

librb_la_SOURCES += $(BUILT_SOURCES)

librb_la_LDFLAGS = -export-dynamic

EXTRA_DIST = rb-marshal.list
