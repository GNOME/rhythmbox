
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
	eggdesktopfile.c				\
	eggdesktopfile.h				\
	eggsmclient.c					\
	eggsmclient.h					\
	eggsmclient-private.h				\
	eggsmclient-xsmp.c				\
	rb-file-helpers.c				\
	rb-file-helpers.h				\
	rb-builder-helpers.c				\
	rb-builder-helpers.h				\
	rb-stock-icons.c				\
	rb-stock-icons.h				\
	rb-cut-and-paste-code.c				\
	rb-cut-and-paste-code.h				\
	rb-util.c					\
	rb-util.h					\
	rb-tree-dnd.c					\
	rb-tree-dnd.h					\
	rb-preferences.h				\
	rb-string-value-map.c				\
	rb-string-value-map.h				\
	rb-async-queue-watch.c				\
	rb-async-queue-watch.h

INCLUDES =						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-DSHARE_DIR=\"$(pkgdatadir)\"			\
	-DDATADIR=\""$(datadir)"\"			\
	-DEGG_SM_CLIENT_BACKEND_XSMP			\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/widgets				\
	$(HAL_CFLAGS)					\
	$(RHYTHMBOX_CFLAGS)

librb_la_LIBADD = $(HAL_LIBS) -lSM -lICE

BUILT_SOURCES=

CLEAN_FILES = $(BUILT_SOURCES)

rb-marshal.c: rb-marshal.list
	@GLIB_GENMARSHAL@ --prefix=rb_marshal $(srcdir)/rb-marshal.list --header --body > rb-marshal.c

rb-marshal.h: rb-marshal.list
	@GLIB_GENMARSHAL@ --prefix=rb_marshal $(srcdir)/rb-marshal.list --header > rb-marshal.h

# inline icons
rhythmbox-set-star-inline.h: $(top_srcdir)/data/icons/hicolor/16x16/status/rhythmbox-set-star.png
	@GDK_PIXBUF_CSOURCE@ --raw --name=rhythmbox_set_star_inline $< > $@

rhythmbox-unset-star-inline.h: $(top_srcdir)/data/icons/hicolor/16x16/status/rhythmbox-unset-star.png
	@GDK_PIXBUF_CSOURCE@ --raw --name=rhythmbox_unset_star_inline $< > $@

rhythmbox-no-star-inline.h: $(top_srcdir)/data/icons/hicolor/16x16/status/rhythmbox-no-star.png
	@GDK_PIXBUF_CSOURCE@ --raw --name=rhythmbox_no_star_inline $< > $@


BUILT_SOURCES += 				\
	rb-marshal.c 				\
	rb-marshal.h 				\
	rhythmbox-set-star-inline.h		\
	rhythmbox-unset-star-inline.h		\
	rhythmbox-no-star-inline.h

librb_la_SOURCES += $(BUILT_SOURCES)

librb_la_LDFLAGS = -export-dynamic

EXTRA_DIST = rb-marshal.list
