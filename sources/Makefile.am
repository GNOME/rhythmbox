include $(top_srcdir)/common.mk

noinst_LTLIBRARIES = libsources.la

libsources_la_SOURCES =			\
	rb-source.c			\
	rb-source.h			\
	rb-library-source.c		\
	rb-library-source.h		\
	rb-song-info-helpers.c		\
	rb-song-info-helpers.h		\
	rb-iradio-source.c		\
	rb-iradio-source.h		
#       rb-group-source.c               \
#       rb-group-source.h		

#if AUDIOCD
#libsources_la_SOURCES += rb-audiocd-source.c		\
#			 rb-audiocd-source.h
#endif

AM_CPPFLAGS +=						\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/monkey-media 			\
	-I$(top_srcdir)/lib 				\
	-I$(top_srcdir)/lib/widgets 			\
	-I$(top_srcdir)/library 			\
	-I$(top_srcdir)/iradio

libsources_la_LDFLAGS = -export-dynamic
