/*
 * arch-tag: test program for retrieval of metadata from MonkeyMedia
 * compile with:
 * gcc bitrate.c -o bitrate `gnome-config --libs --cflags vfs`
 *
 * Bastien Nocera <hadess@hadess.net>
 */

#include <libgnomevfs/gnome-vfs.h>
#include <libgnomevfs/gnome-vfs-mime-utils.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <monkey-media/monkey-media.h>
#include <monkey-media/monkey-media-stream-info.h>

void
print_result (MonkeyMediaStreamInfo *i)
{
	GValue val = {0, };
	int j;

	if (monkey_media_stream_info_get_value (i, MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE, 0, &val)) {
		g_print ("title:	%s\n", g_value_get_string (&val));
		g_value_unset (&val);
	}
	else
		g_print ("(no title available)\n");
	if (monkey_media_stream_info_get_value (i, MONKEY_MEDIA_STREAM_INFO_FIELD_ARTIST, 0, &val)) {
		g_print ("artist:	%s\n", g_value_get_string (&val));
		g_value_unset (&val);
	}
	else
		g_print ("(no artist available)\n");
	if (monkey_media_stream_info_get_value (i, MONKEY_MEDIA_STREAM_INFO_FIELD_ALBUM, 0, &val)) {
		g_print ("album:	%s\n", g_value_get_string (&val));
		g_value_unset (&val);
	}
	else
		g_print ("(no album available)\n");
	if (monkey_media_stream_info_get_value (i, MONKEY_MEDIA_STREAM_INFO_FIELD_DATE, 0, &val)) {
		g_print ("date:		%s\n", g_value_get_string (&val));
		g_value_unset (&val);
	}
	else
		g_print ("(no date available)\n");

	for (j = 0; j < monkey_media_stream_info_get_n_values (i, MONKEY_MEDIA_STREAM_INFO_FIELD_GENRE); j++) {
		if (monkey_media_stream_info_get_value (i, MONKEY_MEDIA_STREAM_INFO_FIELD_GENRE,
							j, &val)) {
			g_print ("genre:	%s\n", g_value_get_string (&val));
			g_value_unset (&val);
		}
	}
	
	if (monkey_media_stream_info_get_value (i, MONKEY_MEDIA_STREAM_INFO_FIELD_COMMENT, 0, &val)) {
		g_print ("comment:	%s\n", g_value_get_string (&val));
		g_value_unset (&val);
	}
	else
		g_print ("(no comment available)\n");
	if (monkey_media_stream_info_get_value (i, MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_CODEC_INFO, 0, &val)) {
		g_print ("audiocodecinfo:	%s\n", g_value_get_string (&val));
		g_value_unset (&val);
	}
	else
		g_print ("(no audiocodecinfo available)\n");
	if (monkey_media_stream_info_get_value (i, MONKEY_MEDIA_STREAM_INFO_FIELD_TRACK_NUMBER, 0, &val)) {
		g_print ("tracknum:	%d\n", g_value_get_int (&val));
		g_value_unset (&val);
	}
	else
		g_print ("(no tracknum available)\n");
	if (monkey_media_stream_info_get_value (i, MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_BIT_RATE, 0, &val)) {
		g_print ("audiobitrate:	%d\n", g_value_get_int (&val));
		g_value_unset (&val);
	}
	else
		g_print ("(no audiobitrate available)\n");
	if (monkey_media_stream_info_get_value (i, MONKEY_MEDIA_STREAM_INFO_FIELD_FILE_SIZE, 0, &val)) {
		g_print ("filesize:	%ld\n", g_value_get_long (&val));
		g_value_unset (&val);
	}
	else
		g_print ("(no filesize available)\n");
	if (monkey_media_stream_info_get_value (i, MONKEY_MEDIA_STREAM_INFO_FIELD_DURATION, 0, &val)) {
		g_print ("length:	%ld\n", g_value_get_long (&val));
		g_value_unset (&val);
	}
	else
		g_print ("(no length available)\n");
}

static int
do_file (const char *uri)
{
	MonkeyMediaStreamInfo *i;
	GError *err = NULL;

	g_print ("mimetype: %s\n", gnome_vfs_get_mime_type (uri));

	i = monkey_media_stream_info_new (uri, &err);
	if (err != NULL) {
		g_warning ("Failed: %s", err->message);
		g_error_free (err);
		return 1;
	} else {
		print_result(i);
	}
	g_object_unref (G_OBJECT (i));
	return 0;
}

int
main (int argc, char **argv)
{
	int i;
	int ecode = 0;

	if (argc < 2) {
		g_print ("Usage: %s <uris>\n", argv[0]);
		return 1;
	}

	monkey_media_init (&argc, &argv);

	for (i = 1; i < argc; i++)
		ecode |= do_file (argv[i]);

	monkey_media_shutdown ();

	return ecode;
}
