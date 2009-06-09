## arch-tag: Automake rules for the toplevel source

AUTOMAKE_OPTIONS = 1.7

DISTCHECK_CONFIGURE_FLAGS = --disable-schemas-install --enable-gtk-doc --disable-scrollkeeper
distuninstallcheck_listfiles = find . -type f -print | grep -v '^\./var/scrollkeeper'

SUBDIRS = macros lib metadata rhythmdb widgets sources podcast \
	backends bindings shell plugins remote data po help tests doc

EXTRA_DIST = 			\
	autogen.sh		\
	config.h.in		\
	gnome-doc-utils.make	\
	MAINTAINERS		\
	MAINTAINERS.old		\
	DOCUMENTERS		\
	INTERNALS

DISTCLEANFILES =			\
	ChangeLog			\
	gnome-doc-utils.make		\
	intltool-extract		\
	intltool-merge			\
	intltool-update

CLEANFILES = ./po/.intltool-merge-cache

CHANGELOG_START = RHYTHMBOX-0_12_0

ChangeLog:
	@echo Creating $@
	@if test -d "$(srcdir)/.git"; then \
	  (GIT_DIR=$(top_srcdir)/.git ./missing --run git log $(CHANGELOG_START).. --stat) > $@.tmp \
	  && mv -f $@.tmp $@ \
	  || ($(RM) $@.tmp; \
	      echo Failed to generate ChangeLog.  Your ChangeLog may be outdated >&2; \
	      (test -f $@ || echo git-log is required to generate this file >> $@)); \
	else \
	  test -f $@ || \
	  (echo A git checkout and git-log is required to generate ChangeLog >&2 && \
	  echo A git checkout and git-log is required to generate this file >> $@); \
	fi

.PHONY: ChangeLog

