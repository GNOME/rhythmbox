/*
 *  Implementation of DAAP (iTunes Music Sharing) dialogs 
 *  (password & name collision)
 *
 *  Copyright (C) 2005 Charles Schmidt <cschmidt2@emich.edu>
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
 */

#include "rb-daap-dialog.h"
#include <gtk/gtk.h>
#include <libgnome/gnome-i18n.h>
#include <string.h>

static gchar *
encode_base64 (gchar *string)
{
	static const gchar base64chars[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	gint out_index = 0;
	gint outlen = ((strlen (string) * 4) / 3) + 4;
	gchar *out = g_malloc (outlen + 1);
	
	memset (out, 0, outlen);

	while (string[0]) {
		int index;

		/* first 6 bits from string[0] */
		index = (string[0] & 0xFC) >> 2;
		out[out_index++] = base64chars[index];

		/* last 2 bits from string[0] and 6 bits from string[1] */
		index = (string[0] & 0x3) << 4;
		index |= (string[1] & 0xF0) >> 4;
		out[out_index++] = base64chars[index];

		/* but if string[1] was 0, it's the final char. fill the rest with pad */
	 	if (!string[1]) {
			out[out_index++] = '=';
			out[out_index++] = '=';
			break;
		}

		/* last 4 bits from string[1] and 2 bits from string[2] */
		index = (string[1] & 0x0F) << 2;
		index |= (string[2] & 0xC0) >> 6;
		out[out_index++] = base64chars[index];

		/* but if string[2] was 0, it was the final char. */
		if (!string[2]) {
			out[out_index++] = '=';
			break;
		}

		/* finally, last 6 bits of string[2] */
		index = (string[2] & 0x3F);
		out[out_index++] = base64chars[index];

		string += 3;
	}
	out[out_index++] = 0;

	return out;
}


gchar * 
rb_daap_password_dialog_new_run (const gchar *name)
{
	GtkWidget *dialog;
	GtkWidget *vbox;
	GtkWidget *hbox;
	gchar *s;
	GtkWidget *label;
	GtkWidget *entry;
	gint resp;
	gchar *ret;

	dialog = gtk_dialog_new_with_buttons (_("Password"),
					      NULL,
					      0,
					      GTK_STOCK_CANCEL,
					      GTK_RESPONSE_CANCEL,
					      GTK_STOCK_OK,
					      GTK_RESPONSE_OK,
					      NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
	
	vbox = gtk_vbox_new (FALSE, 5);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 5);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), vbox, TRUE, TRUE, 0);

	s = g_strconcat (_("The music share '"), 
			 name, 
			 _("' requires a password to connect"), 
			 NULL);
	label = gtk_label_new_with_mnemonic (s);
	g_free (s);
	gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

	hbox = gtk_hbox_new (FALSE, 5);
	gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);
	
	label = gtk_label_new_with_mnemonic (_("_Password"));
	gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
	
	entry = gtk_entry_new ();
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);
	gtk_entry_set_visibility (GTK_ENTRY (entry), FALSE);
	gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
	gtk_box_pack_start (GTK_BOX (hbox), entry, TRUE, TRUE, 0);

	gtk_widget_show_all (dialog);

	resp = gtk_dialog_run (GTK_DIALOG (dialog));

	switch (resp) {
		case GTK_RESPONSE_OK: {
			const gchar *pw = gtk_entry_get_text (GTK_ENTRY (entry));
			gchar *s;

			s = g_malloc0 (strlen (pw) + 2);
			s[0] = ':';
			strcpy (s+1, pw);

			ret = encode_base64 (s);
			g_free (s);
			break;
	      	}
		case GTK_RESPONSE_CANCEL:
		default:
			ret = g_strdup ("");
			break;
	}

	gtk_widget_destroy (dialog);
	
	return ret;
}
	

	
gchar *
rb_daap_collision_dialog_new_run (const gchar *old_name)
{
	GtkWidget *dialog;
	GtkWidget *vbox;
	gchar *s;
	GtkWidget *label;
	GtkWidget *hbox;
	GtkWidget *entry;
	gint resp;

	dialog = gtk_dialog_new_with_buttons (_("Invalid share name"),
					      NULL,
					      0,
					      GTK_STOCK_OK,
					      GTK_RESPONSE_OK,
					      NULL);
	
	vbox = gtk_vbox_new (FALSE, 5);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 5);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), vbox, TRUE, TRUE, 0);

	s = g_strconcat (_("The share name '"),
			 old_name,
			 _("' is already taken. Please choose another."),
			 NULL);
	label = gtk_label_new (s);
	gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);
	g_free (s);
			
	hbox = gtk_hbox_new (FALSE, 5);
	gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);
			
	label = gtk_label_new_with_mnemonic (_("Shared music _name"));
	gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);

	entry = gtk_entry_new ();
	gtk_box_pack_start (GTK_BOX (hbox), entry, FALSE, FALSE, 0);
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);

	gtk_entry_set_text (GTK_ENTRY (entry), old_name);
	gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
		
	gtk_widget_show_all (dialog);

run:
	resp = gtk_dialog_run (GTK_DIALOG (dialog));
	if (resp == GTK_RESPONSE_OK) {
		gchar *new_name = g_strdup (gtk_entry_get_text (GTK_ENTRY (entry)));			
		gtk_widget_destroy (dialog);

		return new_name; 
	} else {
		goto run;
	}

	return NULL;
}
