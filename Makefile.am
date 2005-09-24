## arch-tag: Automake rules for the toplevel source

AUTOMAKE_OPTIONS = 1.7

DISTCHECK_CONFIGURE_FLAGS = --disable-schemas-install

SUBDIRS = macros lib metadata player rhythmdb widgets sources iradio remote daapsharing shell data po help tests doc

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
	INTERNALS		\
	README.iPod

DISTCLEANFILES =			\
	intltool-extract		\
	intltool-merge			\
	intltool-update

CLEANFILES = ./po/.intltool-merge-cache 	\
	     ./help/C/rhythmbox-C.omf.out	\
	     ./help/ja/rhythmbox-ja.omf.out
