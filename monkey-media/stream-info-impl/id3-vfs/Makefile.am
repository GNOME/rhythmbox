## arch-tag: Automake rules for hacked-up id3 tag library

if USE_ID3VFS
noinst_LTLIBRARIES = libid3-vfs.la

libid3_vfs_la_SOURCES = id3-vfs.c id3-vfs.h field.h file.h global.h tag.h mp3bitrate.c mp3bitrate.h

libid3_vfs_la_LIBADD = $(ID3TAG_LIBS)

INCLUDES = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"MonkeyMedia\"			\
	-I$(top_srcdir) 				\
	$(RHYTHMBOX_CFLAGS)

endif
