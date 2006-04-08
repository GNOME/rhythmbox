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
	-I$(top_srcdir)/metadata                       	\
	-I$(top_srcdir)/player                       	\
	-I$(top_srcdir)/rhythmdb                       	\
	-I$(top_srcdir)/widgets                    	\
	-I$(top_srcdir)/sources                    	\
	-I$(top_srcdir)/iradio                    	\
	-I$(top_srcdir)/podcast                    	\
	-I$(top_srcdir)/remote				\
	-I$(top_builddir)/remote			\
	-I$(top_srcdir)/plugins				\
	-I$(top_srcdir)/shell		\
	$(RHYTHMBOX_CFLAGS)		\
	$(PYGTK_CFLAGS)			\
	$(PYTHON_CFLAGS)		\
	$(AM_CFLAGS)			\
	$(WNOERROR_CFLAGS)

rhythmdb.c: rhythmdb.defs rhythmdb.override
	( cd $(srcdir) && $(PYGTK_CODEGEN) \
		--register $(PYGTK_DEFSDIR)/gtk-types.defs \
		--override $*.override \
		--prefix py$* $(<F) ) > $@ 

rb.c: rb.defs rb.override
	( cd $(srcdir) && $(PYGTK_CODEGEN) \
		--register $(PYGTK_DEFSDIR)/gtk-types.defs \
		--register rhythmdb.defs \
		--override $*.override \
		--prefix py$* $(<F) ) > $@ 

BINDING_HEADERS_SRCDIR_IN = \
	plugins/rb-plugin.h			\
	remote/rb-remote-proxy.h		\
	rhythmdb/rhythmdb.h			\
	rhythmdb/rhythmdb-property-model.h	\
	rhythmdb/rhythmdb-query-model.h		\
	rhythmdb/rhythmdb-query-results.h	\
	shell/rb-shell.h			\
	shell/rb-shell-player.h			\
	sources/rb-source.h			\
	sources/rb-sourcelist.h			\
	sources/rb-sourcelist-model.h		\
	$(NULL)

BINDING_HEADERS_BUILDDIR_IN = 

BINDING_HEADERS_SRCDIR		:= $(addprefix $(top_srcdir)/,$(BINDING_HEADERS_SRCDIR_IN))
BINDING_HEADERS_BUILDDIR	:= $(addprefix $(top_builddir)/,$(BINDING_HEADERS_BUILDDIR_IN))

regenerate-python-binding:
	$(PYGTK_H2DEF) $(sort $(BINDING_HEADERS_SRCDIR) $(BINDING_HEADERS_BUILDDIR)) > rhythmbox.defs.new

BUILT_SOURCES = rhythmdb.c rb.c

EXTRA_DIST = \
	rb.override		\
	rb.defs			\
	rhythmdb.override	\
	rhythmdb.defs

CLEANFILES = $(BUILT_SOURCES)

dist-hook:
	cd $(distdir); rm -f $(BUILT_SOURCES)
