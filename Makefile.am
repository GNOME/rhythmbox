## arch-tag: Automake rules for the toplevel source

AUTOMAKE_OPTIONS = 1.7

DISTCHECK_CONFIGURE_FLAGS = --disable-schemas-install

SUBDIRS = lib monkey-media rhythmdb widgets sources iradio corba shell data po component help tests

EXTRA_DIST = 			\
	autogen.sh		\
	config.h.in		\
	intltool-merge.in	\
	intltool-update.in	\
	intltool-extract.in	\
	rhythmbox.spec		\
	rhythmbox.spec.in	\
	MAINTAINERS		\
	MAINTAINERS.old		\
	DOCUMENTERS		\
	INTERNALS

DISTCLEANFILES =			\
	intltool-extract		\
	intltool-merge			\
	intltool-update

CLEANFILES = ./po/.intltool-merge-cache 	\
	     ./help/C/rhythmbox-C.omf.out
