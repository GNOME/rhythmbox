/*
 *  arch-tag: Implementation of Rhythmbox Glade XML utility functions
 *
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

#include <gmodule.h>
#include <gtk/gtk.h>
#include <string.h>

#include "rb-glade-helpers.h"
#include "rb-file-helpers.h"

/**
 * SECTION:rb-glade-helpers
 * @short_description: helper functions for dealing with glade XML files
 *
 * Some simple helper functions to make it a bit easier to deal with
 * widgets built from glade XML files.
 */

static void glade_signal_connect_func (const gchar *cb_name, GObject *obj, 
			               const gchar *signal_name, const gchar *signal_data,
			               GObject *conn_obj, gboolean conn_after,
			               gpointer user_data);

/**
 * rb_glade_xml_new:
 * @file: filename, either absolute or relative to the data directory
 * @root: the widget node in the file to start building from (or NULL)
 * @user_data: user data to pass to autoconnected signal handlers
 *
 * Locates and reads a glade xml file, automatically connecting
 * signal handlers where possible.  The caller can specify a path
 * relative to the shared data directory, or its 'glade' or 'art'
 * subdirectories.
 *
 * Return value: #GladeXML object built from the file
 */
GladeXML *
rb_glade_xml_new (const char *file,
	          const char *root,
		  gpointer user_data)
{
	GladeXML *xml;
	const char *name;

	g_return_val_if_fail (file != NULL, NULL);

	/* if the first character is /, it's an absolute path, otherwise locate it */
	if (file[0] == G_DIR_SEPARATOR)
		name = file;
	else
		name = rb_file (file);

	xml = glade_xml_new (name, root, NULL);

	glade_xml_signal_autoconnect_full (xml,
					   (GladeXMLConnectFunc) glade_signal_connect_func,
					   user_data);

	return xml;
}

static void
glade_signal_connect_func (const gchar *cb_name, GObject *obj, 
			   const gchar *signal_name, const gchar *signal_data,
			   GObject *conn_obj, gboolean conn_after,
			   gpointer user_data)
{
	/** Module with all the symbols of the program */
	static GModule *mod_self = NULL;
	gpointer handler_func;

 	/* initialize gmodule */
	if (mod_self == NULL)
	{
		mod_self = g_module_open (NULL, 0);
		g_assert (mod_self != NULL);
	}

	if (g_module_symbol (mod_self, cb_name, &handler_func))
	{
		/* found callback */
		if (conn_obj)
		{
			if (conn_after)
			{
				g_signal_connect_object
                                        (obj, signal_name, 
                                         handler_func, conn_obj,
                                         G_CONNECT_AFTER);
			}
			else
			{
				g_signal_connect_object
                                        (obj, signal_name, 
                                         handler_func, conn_obj,
                                         G_CONNECT_SWAPPED);
			}
		}
		else
		{
			/* no conn_obj; use standard connect */
			gpointer data = NULL;
			
			data = user_data;
			
			if (conn_after)
			{
				g_signal_connect_after
					(obj, signal_name, 
					 handler_func, data);
			}
			else
			{
				g_signal_connect
					(obj, signal_name, 
					 handler_func, data);
			}
		}
	}
	else
	{
		g_warning("callback function not found: %s", cb_name);
	}
}

/**
 * rb_glade_boldify_label:
 * @xml: GladeXML instance
 * @name: name of the label to boldify
 *
 * Makes a label built from a glade xml file bold.
 */
void
rb_glade_boldify_label (GladeXML *xml, const char *name)
{
	GtkWidget *widget;

	widget = glade_xml_get_widget (xml, name);

	if (widget == NULL) {
		g_warning ("widget '%s' not found", name);
		return;
	}

	/* this way is probably better, but for some reason doesn't work with
	 * labels with mnemonics.

	static PangoAttrList *pattrlist = NULL;

	if (pattrlist == NULL) {
		PangoAttribute *attr;

		pattrlist = pango_attr_list_new ();
		attr = pango_attr_weight_new (PANGO_WEIGHT_BOLD);
		attr->start_index = 0;
		attr->end_index = G_MAXINT;
		pango_attr_list_insert (pattrlist, attr);
	}
	gtk_label_set_attributes (GTK_LABEL (widget), pattrlist);*/

	gchar *str_final;
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


