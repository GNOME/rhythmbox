
# rhythmbox core shared library lives here

lib_LTLIBRARIES = librhythmbox-core.la

librhythmbox_core_la_SOURCES = 

librhythmbox_core_la_LIBADD = 				\
	$(top_builddir)/sources/libsources.la	        \
	$(top_builddir)/metadata/librbmetadata.la	\
	$(top_builddir)/widgets/librbwidgets.la         \
	$(top_builddir)/rhythmdb/librhythmdb.la		\
	$(top_builddir)/backends/librbbackends.la	\
	$(top_builddir)/lib/librb.la

if ENABLE_TRACK_TRANSFER
librhythmbox_core_la_LIBADD += $(GNOME_MEDIA_PROFILES_LIBS)
endif

librhythmbox_core_la_LDFLAGS = -export-dynamic

