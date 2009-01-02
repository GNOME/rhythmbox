## arch-tag: Automake rules for the toplevel source

AUTOMAKE_OPTIONS = 1.7

DISTCHECK_CONFIGURE_FLAGS = --disable-schemas-install --enable-gtk-doc --disable-scrollkeeper
distuninstallcheck_listfiles = find . -type f -print | grep -v '^\./var/scrollkeeper'

SUBDIRS = macros lib metadata rhythmdb widgets sources podcast \
	backends corelib plugins bindings shell remote data po help tests doc

EXTRA_DIST = 			\
	autogen.sh		\
	config.h.in		\
	gnome-doc-utils.make	\
	MAINTAINERS		\
	MAINTAINERS.old		\
	DOCUMENTERS		\
	INTERNALS

DISTCLEANFILES =			\
	gnome-doc-utils.make		\
	intltool-extract		\
	intltool-merge			\
	intltool-update

CLEANFILES = ./po/.intltool-merge-cache
