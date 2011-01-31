/*
 *  Copyright (C) 2002 Jorn Baayen
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#include "config.h"

#include <gtk/gtk.h>
#include <string.h>

#include "rb-builder-helpers.h"
#include "rb-file-helpers.h"

/**
 * SECTION:rb-builder-helpers
 * @short_description: helper functions for dealing with GtkBuilder files
 *
 * Some simple helper functions to make it a bit easier to deal with
 * widgets built from GtkBuilder files.
 */

/**
 * rb_builder_load:
 * @file: filename, either absolute or relative to the data directory
 * @user_data: user data to pass to autoconnected signal handlers
 *
 * Locates and reads a GtkBuilder file, automatically connecting
 * signal handlers where possible.  The caller can specify a path
 * relative to the shared data directory, or its 'ui' or 'art'
 * subdirectories.
 *
 * Return value: (transfer full): #GtkBuilder object built from the file
 */
GtkBuilder *
rb_builder_load (const char *file, gpointer user_data)
{
	GtkBuilder *builder;
	const char *name;
	GError *error = NULL;

	g_return_val_if_fail (file != NULL, NULL);

	/* if the first character is /, it's an absolute path, otherwise locate it */
	if (file[0] == G_DIR_SEPARATOR)
		name = file;
	else
		name = rb_file (file);

	builder = gtk_builder_new ();
	gtk_builder_set_translation_domain (builder, GETTEXT_PACKAGE);
	if (gtk_builder_add_from_file (builder, name, &error) == 0) {
		g_warning ("Error loading GtkBuilder file %s: %s", name, error->message);
		g_error_free (error);
	}

	gtk_builder_connect_signals (builder, user_data);

	return builder;
}


/**
 * rb_builder_boldify_label:
 * @builder: a #GtkBuilder instance
 * @name: name of the label to boldify
 *
 * Makes a label built from a GtkBuilder file bold.
 */
void
rb_builder_boldify_label (GtkBuilder *builder, const char *name)
{
	GObject *widget;
	gchar *str_final;

	/* once we require gtk+ 2.16 or newer, we can set the attributes on
	 * the labels in the builder files, so we won't need this any more.
	 */

	widget = gtk_builder_get_object (builder, name);
	if (widget == NULL) {
		g_warning ("widget '%s' not found", name);
		return;
	}

	str_final = g_strdup_printf ("<b>%s</b>", gtk_label_get_label (GTK_LABEL (widget)));
	gtk_label_set_markup_with_mnemonic (GTK_LABEL (widget), str_final);
	g_free (str_final);
}

/**
 * rb_combo_box_hyphen_separator_func:
 * @model: a #GtkTreeModel
 * @iter: a #GtkTreeIter
 * @data: nothing
 *
 * A row separator function to use for GtkComboBox widgets.
 * It expects the model to contain a string in its first column,
 * and interprets a string containing a single hyphen character
 * as a separator.
 *
 * Return value: %TRUE if the row pointed to by @iter is a separator
 */
gboolean
rb_combo_box_hyphen_separator_func (GtkTreeModel *model,
				    GtkTreeIter *iter,
				    gpointer data)
{
	const char *s;

	gtk_tree_model_get (model, iter, 0, &s, -1);

	if (s == NULL)
		return FALSE;

	return (strcmp (s, "-") == 0);
}


