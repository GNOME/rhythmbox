
# rhythmbox core shared library lives here

lib_LTLIBRARIES = librhythmbox-core.la

librhythmbox_core_la_SOURCES =
	rb-plugin.h					\
	rb-plugin.c					\
	rb-module.c					\
	rb-module.h					\
	rb-plugin-manager.h				\
	rb-plugin-manager.c				\
	rb-plugins-engine.h				\
	rb-plugins-engine.c


librhythmbox_core_la_LIBADD = 				\
	$(top_builddir)/sources/libsources.la	        \
	$(top_builddir)/metadata/librbmetadata.la	\
	$(top_builddir)/widgets/librbwidgets.la         \
	$(top_builddir)/backends/librbbackends.la	\
	$(top_builddir)/lib/librb.la

librhythmbox_core_la_LDFLAGS = -export-dynamic

