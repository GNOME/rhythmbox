/* 
 *  Copyright (C) 2002 Colin Walters <walters@gnu.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *  $Id$
 */

#include "rb-windows-ini-file.h"

#include "getstr.h"

#include <libgnome/gnome-i18n.h>
#include <string.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <ctype.h>

static void rb_windows_ini_file_class_init (RBWindowsINIFileClass *klass);
static void rb_windows_ini_file_init (RBWindowsINIFile *view);
static void rb_windows_ini_file_finalize (GObject *object);
static void rb_windows_ini_file_set_property (GObject *object,
					      guint prop_id,
					      const GValue *value,
					      GParamSpec *pspec);
static void rb_windows_ini_file_get_property (GObject *object,
					      guint prop_id,
					      GValue *value,
					      GParamSpec *pspec);
static void rb_windows_ini_file_parse_from_stream (RBWindowsINIFile *inifile,
						   FILE *stream);

struct _RBWindowsINIFilePrivate
{
	GHashTable *sections;
	char *filename;
};

static GObjectClass *parent_class = NULL;

enum
{
	PROP_NONE,
	PROP_FILENAME,
};

GType
rb_windows_ini_file_get_type (void)
{
	static GType rb_windows_ini_file_type = 0;

	if (rb_windows_ini_file_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBWindowsINIFileClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_windows_ini_file_class_init,
			NULL,
			NULL,
			sizeof (RBWindowsINIFile),
			0,
			(GInstanceInitFunc) rb_windows_ini_file_init
		};
		
		rb_windows_ini_file_type = g_type_register_static (G_TYPE_OBJECT,
								   "RBWindowsINIFile",
								   &our_info, 0);
		
	}

	return rb_windows_ini_file_type;
}

static void
rb_windows_ini_file_class_init (RBWindowsINIFileClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	parent_class = g_type_class_peek_parent (klass);
	
	object_class->finalize = rb_windows_ini_file_finalize;
	object_class->set_property = rb_windows_ini_file_set_property;
	object_class->get_property = rb_windows_ini_file_get_property;

	g_object_class_install_property (object_class,
					 PROP_FILENAME,
					 g_param_spec_string ("filename",
							      "Filename",
							      "Filename",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
rb_windows_ini_file_free_hash_string (gpointer val)
{
	g_free ((char *) val);
}

static void
rb_windows_ini_file_free_section_hash_entry (gpointer val)
{
	g_hash_table_destroy ((GHashTable *) val);
}

static void
rb_windows_ini_file_init (RBWindowsINIFile *inifile)
{
	inifile->priv = g_new0(RBWindowsINIFilePrivate, 1);
	inifile->priv->sections = g_hash_table_new_full (g_str_hash, g_str_equal,
							 rb_windows_ini_file_free_hash_string,
							 rb_windows_ini_file_free_section_hash_entry);
}

static void
rb_windows_ini_file_finalize (GObject *object)
{
	RBWindowsINIFile *inifile;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_WINDOWS_INI_FILE (object));

	inifile = RB_WINDOWS_INI_FILE (object);

	g_return_if_fail (inifile->priv != NULL);

	g_hash_table_destroy (inifile->priv->sections);

	g_free (inifile->priv->filename);
	g_free (inifile->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_windows_ini_file_set_property (GObject *object,
				  guint prop_id,
				  const GValue *value,
				  GParamSpec *pspec)
{
	RBWindowsINIFile *inifile = RB_WINDOWS_INI_FILE (object);

	switch (prop_id)
	{
	case PROP_FILENAME:
	{
		FILE *f;
		inifile->priv->filename = g_strdup(g_value_get_string (value));
		f = fopen(inifile->priv->filename, "r");
		if (f < 0)
		{
/* 			rb_error_dialog (_("Unable to open %s: %s\n"), */
/* 					 inifile->priv->filename, g_strerror (errno)); */
			fprintf (stderr, "couldn't open %s\n", inifile->priv->filename);
			break;
		}
		rb_windows_ini_file_parse_from_stream (inifile, f);
		fclose (f);
		break;
	}
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_windows_ini_file_get_property (GObject *object,
				  guint prop_id,
				  GValue *value,
				  GParamSpec *pspec)
{
	RBWindowsINIFile *inifile = RB_WINDOWS_INI_FILE (object);

	switch (prop_id)
	{
	case PROP_FILENAME:
	{
		g_value_set_string(value, inifile->priv->filename);
		break;
	}
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBWindowsINIFile *
rb_windows_ini_file_new (const char *filename)
{
	RBWindowsINIFile *inifile = RB_WINDOWS_INI_FILE (g_object_new (RB_TYPE_WINDOWS_INI_FILE,
								       "filename", filename,
								       NULL));
	
	g_return_val_if_fail (inifile->priv != NULL, NULL);
	return inifile;
}

static void
push_key (gpointer keyp, gpointer val, gpointer listptr_p)
{
	char *key = keyp;
	GList **listptr = listptr_p;
	*listptr = g_list_prepend (*listptr, key);
}

static GList *
hash_table_keys (GHashTable *table)
{
	GList *ret = NULL;
	g_hash_table_foreach (table, push_key, &ret);
	return ret;
}

GList *
rb_windows_ini_file_get_sections (RBWindowsINIFile *inifile)
{
	return hash_table_keys (inifile->priv->sections);
}

GList *
rb_windows_ini_file_get_keys (RBWindowsINIFile *inifile, const char *section)
{
	GHashTable *values;
	char *lowerkey;
	GList *ret;
	g_return_val_if_fail (g_utf8_validate (section, -1, NULL), NULL);
	lowerkey = g_utf8_strdown (section, -1);
	values = g_hash_table_lookup (inifile->priv->sections, section);
	g_free (lowerkey);
	g_return_val_if_fail (values != NULL, NULL);
	ret = hash_table_keys (values);
	return ret;
}

const char *
rb_windows_ini_file_lookup (RBWindowsINIFile *inifile, const char *section, const char *key)
{
	GHashTable *values;
	char *lowerkey, *ret;
	g_return_val_if_fail (g_utf8_validate (section, -1, NULL), NULL);
	lowerkey = g_utf8_strdown (section, -1);
	values = g_hash_table_lookup (inifile->priv->sections, section);
	g_free (lowerkey);
	g_return_val_if_fail (values != NULL, NULL);
	g_return_val_if_fail (g_utf8_validate (key, -1, NULL), NULL);
	lowerkey = g_utf8_strdown (key, -1);
	ret = g_hash_table_lookup (values, lowerkey);
	g_free (lowerkey);
	return ret;
}

static void
rb_windows_ini_file_unicodify (char **str)
{
	char *ret;
	int bytes_read, bytes_written;
	if (g_utf8_validate (*str, -1, NULL))
		return;
	/* The defacto encoding for Windows ini files seem to default
	 * to iso-8859-1 */
	ret = g_convert (*str, strlen (*str), "UTF-8", "ISO-8859-1",
			 &bytes_read, &bytes_written, NULL);
	/* Failing that, try the locale's encoding. */
	if (!ret)
		ret = g_locale_to_utf8 (*str, strlen (*str), &bytes_read, &bytes_written, NULL);
	g_free (*str);
	*str = ret;
}

static void
rb_windows_ini_file_parse_from_stream (RBWindowsINIFile *inifile,
				       FILE *stream)
{
	GList *sectionlist = NULL;
	GHashTable *sectiontable = inifile->priv->sections;
 	char *cursection = NULL; 
	GHashTable *defaulthash = g_hash_table_new_full (g_str_hash, g_str_equal,
							 rb_windows_ini_file_free_hash_string,
							 rb_windows_ini_file_free_hash_string);
	GHashTable *cursectionhash = NULL;
	int c;
	char *errmsg;
	char *defaultstr = g_strdup ("DEFAULT");

	g_hash_table_insert (sectiontable, defaultstr, defaulthash);
	sectionlist = g_list_prepend (sectionlist, defaultstr);
	
	while ((c = fgetc(stream)) != EOF)
	{
		if (isspace (c))
			continue;
		if (c == '[')
		{
			int size = 0;
			char *tmp;
			cursection = NULL;
			if (getstr (&cursection, &size, stream, ']', 0, 0) < 0)
			{
				errmsg = g_strdup ("Missing terminating ]");
				goto lose;
			}
			/* Trim the ']', and lowercase */
			cursection = g_strstrip (cursection);
			cursection[strlen(cursection)-1] = '\0';
			rb_windows_ini_file_unicodify (&cursection);
			if (!cursection)
				goto bad_encoding;
			tmp = g_utf8_strdown (cursection, -1);
			g_free (cursection);
			cursection = tmp;

			cursectionhash = g_hash_table_new_full (g_str_hash, g_str_equal,
								rb_windows_ini_file_free_hash_string,
								rb_windows_ini_file_free_hash_string);

			g_hash_table_insert (sectiontable, cursection, cursectionhash);
			sectionlist = g_list_prepend (sectionlist, cursection);
		}
		else if (isalpha (c) || isdigit (c))
		{
			int size = 0;
			char *curident = NULL;
			char *curvalue = NULL;
			char *tmp;
			ungetc (c, stream);
			if (getstr (&curident, &size, stream, '=', 0, 0) < 0)
			{
				errmsg = g_strdup ("Missing = after identifier");
				goto lose;
			}
			/* Trim the '=', and lowercase */
			curident = g_strstrip (curident);
			curident[strlen(curident)-1] = '\0';
			rb_windows_ini_file_unicodify (&curident);
			if (!curident)
				goto bad_encoding;
			tmp = g_utf8_strdown (curident, -1);
			g_free (curident);
			curident = tmp;

			size = 0;
			if (getstr (&curvalue, &size, stream, '\n', 0, 0) < 0)
			{
				errmsg = g_strdup ("Missing newline after value");
				goto lose;
			}
			curvalue[strlen(curvalue)-1] = '\0';
			rb_windows_ini_file_unicodify (&curvalue);
			if (!curvalue)
				goto bad_encoding;
			g_hash_table_insert (cursectionhash ? cursectionhash : defaulthash, curident, curvalue);
		}
		else
		{
			errmsg = g_strdup_printf ("Unknown character %c", c);
			goto lose;
		}
			
	}
	
	
	return;
bad_encoding:
	errmsg = g_strdup ("Unable to determine file encoding");
lose:
	fprintf (stderr, _("Unable to parse %s: %s\n"), inifile->priv->filename, errmsg);
/*  	rb_error_dialog (_("Unable to parse %s\n"), inifile->priv->filename);  */
}

#ifdef RB_DEBUG_INIFILE
int
main (int argc, char **argv)
{
	RBWindowsINIFile *inifile;
	GList *cur, *sections;
	gtk_init (&argc, &argv);

	if (argc != 2)
	{
		fprintf (stderr, "usage: rb-windows-ini-file filename\n");
		exit(1);
	}
	inifile = rb_windows_ini_file_new (argv[1]);
	sections = rb_windows_ini_file_get_sections (inifile);
	for (cur = sections; cur; cur = cur->next)
	{
		GList *values = rb_windows_ini_file_get_keys (inifile, (char *) cur->data);
		fprintf (stdout, "Section: %s\n", (char *) cur->data);
		for (; values; values = values->next)
		{
			const char *value; 
			fprintf (stdout, "Key: %s\n", (char *) values->data);
			value = rb_windows_ini_file_lookup (inifile, (char *) cur->data, (char *) values->data);
			fprintf (stdout, "Value: %s\n", value);
		}
	}
	g_object_unref (inifile);
	exit (0);
}
#endif
