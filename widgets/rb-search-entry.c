/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
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

#include <config.h>

#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "rb-search-entry.h"

static void rb_search_entry_class_init (RBSearchEntryClass *klass);
static void rb_search_entry_init (RBSearchEntry *entry);
static void rb_search_entry_finalize (GObject *object);
static gboolean rb_search_entry_timeout_cb (RBSearchEntry *entry);
static void rb_search_entry_changed_cb (GtkEditable *editable,
			                RBSearchEntry *entry);
static void rb_search_entry_activate_cb (GtkEntry *gtkentry,
					 RBSearchEntry *entry);
static void rb_search_entry_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void rb_search_entry_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void button_clicked_cb (GtkButton *button, RBSearchEntry *entry);
static gboolean rb_search_entry_focus_out_event_cb (GtkWidget *widget,
				                    GdkEventFocus *event,
				                    RBSearchEntry *entry);
static void rb_search_entry_clear_cb (GtkEntry *entry,
				      GtkEntryIconPosition icon_pos,
				      GdkEvent *event,
				      RBSearchEntry *search_entry);
static void rb_search_entry_check_style (RBSearchEntry *entry);

struct RBSearchEntryPrivate
{
	GtkWidget *label;
	GtkWidget *entry;
	GtkWidget *button;

	gboolean explicit_mode;
	gboolean clearing;
	gboolean searching;

	guint timeout;

	gboolean is_a11y_theme;
};

G_DEFINE_TYPE (RBSearchEntry, rb_search_entry, GTK_TYPE_HBOX)
#define RB_SEARCH_ENTRY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_SEARCH_ENTRY, RBSearchEntryPrivate))

/**
 * SECTION:rb-search-entry
 * @short_description: text entry widget for the search box
 *
 * The search entry contains a label and a text entry box.
 * The text entry box contains an icon that acts as a 'clear'
 * button.
 *
 * Signals are emitted when the search text changes,
 * arbitrarily rate-limited to one every 300ms.
 *
 * When the text entry widget is non-empty, its colours are
 * changed to display the text in black on yellow.
 */

enum
{
	SEARCH,
	ACTIVATE,
	LAST_SIGNAL
};

enum
{
	PROP_0,
	PROP_EXPLICIT_MODE
};

static guint rb_search_entry_signals[LAST_SIGNAL] = { 0 };

static void
rb_search_entry_class_init (RBSearchEntryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = rb_search_entry_finalize;
	object_class->set_property = rb_search_entry_set_property;
	object_class->get_property = rb_search_entry_get_property;

	/**
	 * RBSearchEntry::search:
	 * @entry: the #RBSearchEntry
	 * @text: search text
	 *
	 * Emitted when the search text changes.  A signal
	 * handler must initiate a search on the current
	 * source.
	 */
	rb_search_entry_signals[SEARCH] =
		g_signal_new ("search",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBSearchEntryClass, search),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);

	/**
	 * RBSearchEntry::activate:
	 * @entry: the #RBSearchEntry
	 * @text: search text
	 *
	 * Emitted when the entry is activated.
	 */
	rb_search_entry_signals[ACTIVATE] =
		g_signal_new ("activate",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBSearchEntryClass, activate),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);

	/**
	 * RBSearchEntry:explicit-mode:
	 *
	 * If TRUE, show a button and only emit the 'search' signal when
	 * the user presses it rather than when they stop typing.
	 */
	g_object_class_install_property (object_class,
					 PROP_EXPLICIT_MODE,
					 g_param_spec_boolean ("explicit-mode",
							       "explicit mode",
							       "whether in explicit search mode or not",
							       FALSE,
							       G_PARAM_READWRITE));

	g_type_class_add_private (klass, sizeof (RBSearchEntryPrivate));
}

static void
rb_search_entry_init (RBSearchEntry *entry)
{
	GtkSettings *settings;
	char *theme;

	entry->priv = RB_SEARCH_ENTRY_GET_PRIVATE (entry);

	settings = gtk_settings_get_for_screen (gtk_widget_get_screen (GTK_WIDGET (entry)));
	g_object_get (settings, "gtk-theme-name", &theme, NULL);
	entry->priv->is_a11y_theme = strncmp (theme, "HighContrast", strlen ("HighContrast")) == 0 ||
					strncmp (theme, "LowContrast", strlen ("LowContrast")) == 0;
	g_free (theme);

	/* this string can only be so long, or there wont be a search entry :) */
	entry->priv->label = gtk_label_new_with_mnemonic (_("_Search:"));
	gtk_label_set_justify (GTK_LABEL (entry->priv->label), GTK_JUSTIFY_RIGHT);
	gtk_box_pack_start (GTK_BOX (entry), entry->priv->label, FALSE, TRUE, 0);
	gtk_widget_set_no_show_all (entry->priv->label, TRUE);
	gtk_widget_show (entry->priv->label);

	entry->priv->entry = gtk_entry_new ();
	gtk_entry_set_icon_from_stock (GTK_ENTRY (entry->priv->entry),
				       GTK_ENTRY_ICON_SECONDARY,
				       GTK_STOCK_CLEAR);
	gtk_entry_set_icon_tooltip_text (GTK_ENTRY (entry->priv->entry),
					 GTK_ENTRY_ICON_SECONDARY,
					 _("Clear the search text"));
	g_signal_connect_object (GTK_ENTRY (entry->priv->entry),
				 "icon-press",
				 G_CALLBACK (rb_search_entry_clear_cb),
				 entry, 0);

	gtk_label_set_mnemonic_widget (GTK_LABEL (entry->priv->label),
				       entry->priv->entry);

	gtk_box_pack_start (GTK_BOX (entry), entry->priv->entry, TRUE, TRUE, 0);

	g_signal_connect_object (G_OBJECT (entry->priv->entry),
				 "changed",
				 G_CALLBACK (rb_search_entry_changed_cb),
				 entry, 0);
	g_signal_connect_object (G_OBJECT (entry->priv->entry),
				 "focus_out_event",
				 G_CALLBACK (rb_search_entry_focus_out_event_cb),
				 entry, 0);
	g_signal_connect_object (G_OBJECT (entry->priv->entry),
				 "activate",
				 G_CALLBACK (rb_search_entry_activate_cb),
				 entry, 0);

	entry->priv->button = gtk_button_new_with_label (_("Search"));
	gtk_box_pack_start (GTK_BOX (entry), entry->priv->button, FALSE, FALSE, 0);
	gtk_widget_set_no_show_all (entry->priv->button, TRUE);
	g_signal_connect_object (entry->priv->button,
				 "clicked",
				 G_CALLBACK (button_clicked_cb),
				 entry, 0);
}

static void
rb_search_entry_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	RBSearchEntry *entry = RB_SEARCH_ENTRY (object);

	switch (prop_id) {
	case PROP_EXPLICIT_MODE:
		entry->priv->explicit_mode = g_value_get_boolean (value);
		gtk_widget_set_visible (entry->priv->label, entry->priv->explicit_mode == FALSE);
		gtk_widget_set_visible (entry->priv->button, entry->priv->explicit_mode == TRUE);
		rb_search_entry_check_style (entry);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_search_entry_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	RBSearchEntry *entry = RB_SEARCH_ENTRY (object);

	switch (prop_id) {
	case PROP_EXPLICIT_MODE:
		g_value_set_boolean (value, entry->priv->explicit_mode);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_search_entry_finalize (GObject *object)
{
	RBSearchEntry *entry;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SEARCH_ENTRY (object));

	entry = RB_SEARCH_ENTRY (object);

	g_return_if_fail (entry->priv != NULL);

	G_OBJECT_CLASS (rb_search_entry_parent_class)->finalize (object);
}

/**
 * rb_search_entry_new:
 *
 * Creates a new search entry widget.
 *
 * Return value: new search entry widget.
 */
RBSearchEntry *
rb_search_entry_new (void)
{
	RBSearchEntry *entry;

	entry = RB_SEARCH_ENTRY (g_object_new (RB_TYPE_SEARCH_ENTRY,
					       "spacing", 5,
					       NULL));

	g_return_val_if_fail (entry->priv != NULL, NULL);

	return entry;
}

/**
 * rb_search_entry_clear:
 * @entry: a #RBSearchEntry
 *
 * Clears the search entry text.  The 'search' signal will
 * be emitted.
 */
void
rb_search_entry_clear (RBSearchEntry *entry)
{
	if (entry->priv->timeout != 0) {
		g_source_remove (entry->priv->timeout);
		entry->priv->timeout = 0;
	}

	entry->priv->clearing = TRUE;

	gtk_entry_set_text (GTK_ENTRY (entry->priv->entry), "");

	entry->priv->clearing = FALSE;
}

/**
 * rb_search_entry_set_text:
 * @entry: a #RBSearchEntry
 * @text: new search text
 *
 * Sets the text in the search entry box.
 * The 'search' signal will be emitted.
 */
void
rb_search_entry_set_text (RBSearchEntry *entry, const char *text)
{
	gtk_entry_set_text (GTK_ENTRY (entry->priv->entry),
			    text ? text : "");
}

static void
rb_search_entry_check_style (RBSearchEntry *entry)
{
	static const GdkRGBA fallback_bg_color = { 0.9686, 0.9686, 0.7451, 1.0}; /* yellow-ish */
	static const GdkRGBA fallback_fg_color = { 0, 0, 0, 1.0 }; /* black. */
	GdkRGBA bg_color = {0,};
	GdkRGBA fg_color = {0,};
	const gchar* text;
	gboolean searching;

	if (entry->priv->is_a11y_theme)
		return;

	/* allow user style to override the colors */
	if (gtk_style_context_lookup_color (gtk_widget_get_style_context (GTK_WIDGET (entry)),
					    "rb-search-active-bg",
					    &bg_color) == FALSE) {
		bg_color = fallback_bg_color;
	}
	if (gtk_style_context_lookup_color (gtk_widget_get_style_context (GTK_WIDGET (entry)),
					    "rb-search-active-fg",
					    &fg_color) == FALSE) {
		fg_color = fallback_fg_color;
	}

	if (entry->priv->explicit_mode) {
		searching = entry->priv->searching;
	} else {
		text = gtk_entry_get_text (GTK_ENTRY (entry->priv->entry));
		searching = (text && *text);
	}

	if (searching) {
		gtk_widget_override_color (entry->priv->entry, GTK_STATE_NORMAL, &fg_color);
		gtk_widget_override_background_color (entry->priv->entry, GTK_STATE_NORMAL, &bg_color);
		gtk_widget_override_cursor (entry->priv->entry, &fg_color, &fg_color);
	} else {
		gtk_widget_override_color (entry->priv->entry, GTK_STATE_NORMAL, NULL);
		gtk_widget_override_background_color (entry->priv->entry, GTK_STATE_NORMAL, NULL);
		gtk_widget_override_cursor (entry->priv->entry, NULL, NULL);
	}

	gtk_widget_queue_draw (GTK_WIDGET (entry));
}

static void
rb_search_entry_changed_cb (GtkEditable *editable,
			    RBSearchEntry *entry)
{
	const char *text;

	if (entry->priv->clearing == TRUE) {
		entry->priv->searching = FALSE;
		rb_search_entry_check_style (entry);
		return;
	}

	if (entry->priv->timeout != 0) {
		g_source_remove (entry->priv->timeout);
		entry->priv->timeout = 0;
	}

	/* emit it now if we're clearing the entry */
	text = gtk_entry_get_text (GTK_ENTRY (entry->priv->entry));
	if (text != NULL && text[0] != '\0') {
		gtk_widget_set_sensitive (entry->priv->button, TRUE);
		entry->priv->timeout = g_timeout_add (300, (GSourceFunc) rb_search_entry_timeout_cb, entry);
	} else {
		entry->priv->searching = FALSE;
		gtk_widget_set_sensitive (entry->priv->button, FALSE);
		rb_search_entry_timeout_cb (entry);
	}
	rb_search_entry_check_style (entry);
}

static gboolean
rb_search_entry_timeout_cb (RBSearchEntry *entry)
{
	const char *text;
	gdk_threads_enter ();

	text = gtk_entry_get_text (GTK_ENTRY (entry->priv->entry));

	if (entry->priv->explicit_mode == FALSE) {
		g_signal_emit (G_OBJECT (entry), rb_search_entry_signals[SEARCH], 0, text);
	}
	entry->priv->timeout = 0;

	gdk_threads_leave ();

	return FALSE;
}

static gboolean
rb_search_entry_focus_out_event_cb (GtkWidget *widget,
				    GdkEventFocus *event,
				    RBSearchEntry *entry)
{
	if (entry->priv->timeout == 0)
		return FALSE;

	g_source_remove (entry->priv->timeout);
	entry->priv->timeout = 0;

	if (entry->priv->explicit_mode == FALSE) {
		g_signal_emit (G_OBJECT (entry), rb_search_entry_signals[SEARCH], 0,
			       gtk_entry_get_text (GTK_ENTRY (entry->priv->entry)));
	}

	return FALSE;
}

/**
 * rb_search_entry_searching:
 * @entry: a #RBSearchEntry
 *
 * Returns %TRUE if there is search text in the entry widget.
 *
 * Return value: %TRUE if searching
 */
gboolean
rb_search_entry_searching (RBSearchEntry *entry)
{
	if (entry->priv->explicit_mode) {
		return entry->priv->searching;
	} else {
		return strcmp ("", gtk_entry_get_text (GTK_ENTRY (entry->priv->entry))) != 0;
	}
}

static void
rb_search_entry_activate_cb (GtkEntry *gtkentry,
			     RBSearchEntry *entry)
{
	entry->priv->searching = TRUE;
	rb_search_entry_check_style (entry);
	g_signal_emit (G_OBJECT (entry), rb_search_entry_signals[ACTIVATE], 0,
		       gtk_entry_get_text (GTK_ENTRY (entry->priv->entry)));
}

static void
button_clicked_cb (GtkButton *button, RBSearchEntry *entry)
{
	entry->priv->searching = TRUE;
	rb_search_entry_check_style (entry);
	g_signal_emit (G_OBJECT (entry), rb_search_entry_signals[SEARCH], 0,
		       gtk_entry_get_text (GTK_ENTRY (entry->priv->entry)));
}

/**
 * rb_search_entry_grab_focus:
 * @entry: a #RBSearchEntry
 *
 * Grabs input focus for the text entry widget.
 */
void
rb_search_entry_grab_focus (RBSearchEntry *entry)
{
	gtk_widget_grab_focus (GTK_WIDGET (entry->priv->entry));
}

static void
rb_search_entry_clear_cb (GtkEntry *entry,
			  GtkEntryIconPosition icon_pos,
			  GdkEvent *event,
			  RBSearchEntry *search_entry)
{
	rb_search_entry_set_text (search_entry, "");
}
