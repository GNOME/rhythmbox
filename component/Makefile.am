## arch-tag: Automake rules for the Nautilus component

INCLUDES =						\
	-DG_DISABLE_DEPRECATED				\
	-DGTK_DISABLE_DEPRECATED			\
	-DGNOME_DISABLE_DEPRECATED			\
	-DDATADIR=\"$(datadir)\"			\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	$(DISABLE_DEPRECATED_CFLAGS)			\
	$(RHYTHMBOX_CFLAGS)

bonobodir = $(libdir)/bonobo
bonobo_LTLIBRARIES = librb-nautilus-context-menu.la

librb_nautilus_context_menu_la_SOURCES =			\
	rb-nautilus-context-menu.c			\
	rb-nautilus-context-menu.h			\
	libmain.c

librb_nautilus_context_menu_la_LIBADD = $(top_builddir)/corba/librbcorba.la $(RHYTHMBOX_LIBS)
librb_nautilus_context_menu_la_LDFLAGS = -module -avoid-version

server_in_files = Rhythmbox_Nautilus_Context_Menu.server.in.in

serverdir = $(libdir)/bonobo/servers
server_DATA = $(server_in_files:.server.in.in=.server)
$(server_in_files:.server.in.in=.server.in): $(server_in_files)
	sed -e "s|\@BONOBODIR\@|$(bonobodir)|" $< > $@

@INTLTOOL_SERVER_RULE@

EXTRA_DIST = $(server_in_files)
CLEANFILES = $(server_DATA) $(server_in_files:.server.in.in=.server.in)
