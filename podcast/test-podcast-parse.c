
#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>

#include <libgnomevfs/gnome-vfs.h>

#include "rb-podcast-parse.h"

#include <string.h>

static gboolean debug = FALSE;

void rb_debug_real (const char *func,
		    const char *file,
		    int line,
		    gboolean newline,
		    const char *format, ...) G_GNUC_PRINTF (5, 6);

/* For the benefit of the podcast parsing code */
void
rb_debug_real (const char *func,
	       const char *file,
	       int line,
	       gboolean newline,
	       const char *format, ...)
{
	va_list args;
	char buffer[1025];

	if (debug == FALSE)
		return;

	va_start (args, format);
	g_vsnprintf (buffer, 1024, format, args);
	va_end (args);

	g_printerr (newline ? "%s:%d [%s] %s\n" : "%s:%d [%s] %s",
		    file, line, func, buffer);
}

int main (int argc, char **argv)
{
	RBPodcastChannel *data;
	GList *l;
	GDate date = {0,};
	char datebuf[1024];

	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

	gnome_vfs_init ();

	if (argv[2] != NULL && strcmp (argv[2], "--debug") == 0) {
		debug = TRUE;
	}

	data = g_new0 (RBPodcastChannel, 1);
	if (rb_podcast_parse_load_feed (data, argv[1]) == FALSE) {
		g_warning ("Couldn't parse %s", argv[1]);
		return 1;
	}

	g_date_set_time_t (&date, data->pub_date);
	g_date_strftime (datebuf, 1024, "%F %T", &date);

	g_print ("Podcast title: %s\n", data->title);
	g_print ("Summary: %s\n", data->summary);
	g_print ("Description: %s\n", data->description);
	g_print ("Author: %s\n", data->author);
	g_print ("Date: %s\n", datebuf);
	g_print ("\n");

	for (l = data->posts; l != NULL; l = l->next) {
		RBPodcastItem *item = l->data;

		g_date_set_time_t (&date, item->pub_date);
		g_date_strftime (datebuf, 1024, "%F %T", &date);

		g_print ("\tItem title: %s\n", item->title);
		g_print ("\tURL: %s\n", item->url);
		g_print ("\tAuthor: %s\n", item->author);
		g_print ("\tDate: %s\n", datebuf);
		g_print ("\tDescription: %s\n", item->description);
		g_print ("\n");
	}

	return 0;
}

