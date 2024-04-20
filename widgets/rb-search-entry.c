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
#include "rb-util.h"

static void rb_search_entry_class_init (RBSearchEntryClass *klass);
static void rb_search_entry_init (RBSearchEntry *entry);
static void rb_search_entry_constructed (GObject *object);
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
static void rb_search_entry_update_icons (RBSearchEntry *entry);
static void rb_search_entry_widget_grab_focus (GtkWidget *widget);

struct RBSearchEntryPrivate
{
	GtkWidget *entry;
	GtkWidget *button;

	gboolean has_popup;
	gboolean explicit_mode;
	gboolean clearing;
	gboolean searching;

	guint timeout;
};

G_DEFINE_TYPE (RBSearchEntry, rb_search_entry, GTK_TYPE_BOX)
#define RB_SEARCH_ENTRY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_SEARCH_ENTRY, RBSearchEntryPrivate))

/**
 * SECTION:rbsearchentry
 * @short_description: text entry widget for the search box
 *
 * The search entry contains a label and a text entry box.
 * The text entry box contains an icon that acts as a 'clear'
 * button.
 *
 * Signals are emitted when the search text changes,
 * arbitrarily rate-limited to one every 300ms.
 */

enum
{
	SEARCH,
	ACTIVATE,
	SHOW_POPUP,
	LAST_SIGNAL
};

enum
{
	PROP_0,
	PROP_EXPLICIT_MODE,
	PROP_HAS_POPUP
};

static guint rb_search_entry_signals[LAST_SIGNAL] = { 0 };

static void
rb_search_entry_class_init (RBSearchEntryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->constructed = rb_search_entry_constructed;
	object_class->finalize = rb_search_entry_finalize;
	object_class->set_property = rb_search_entry_set_property;
	object_class->get_property = rb_search_entry_get_property;

	widget_class->grab_focus = rb_search_entry_widget_grab_focus;

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
			      NULL,
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
			      NULL,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);

	/**
	 * RBSearchEntry::show-popup:
	 * @entry: the #RBSearchEntry
	 *
	 * Emitted when a popup menu should be shown
	 */
	rb_search_entry_signals[SHOW_POPUP] =
		g_signal_new ("show-popup",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBSearchEntryClass, show_popup),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      0);

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
	/**
	 * RBSearchEntry:has-popup:
	 *
	 * If TRUE, show a primary icon and emit the show-popup when clicked.
	 */
	g_object_class_install_property (object_class,
					 PROP_HAS_POPUP,
					 g_param_spec_boolean ("has-popup",
							       "has popup",
							       "whether to display the search menu icon",
							       FALSE,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (RBSearchEntryPrivate));
}

static void
rb_search_entry_init (RBSearchEntry *entry)
{
	entry->priv = RB_SEARCH_ENTRY_GET_PRIVATE (entry);
	gtk_orientable_set_orientation (GTK_ORIENTABLE (entry), GTK_ORIENTATION_HORIZONTAL);
}

static void
rb_search_entry_constructed (GObject *object)
{
	RBSearchEntry *entry;

	RB_CHAIN_GOBJECT_METHOD (rb_search_entry_parent_class, constructed, object);

	entry = RB_SEARCH_ENTRY (object);

	entry->priv->entry = gtk_entry_new ();
	g_signal_connect_object (GTK_ENTRY (entry->priv->entry),
				 "icon-press",
				 G_CALLBACK (rb_search_entry_clear_cb),
				 entry, 0);

	gtk_entry_set_icon_tooltip_text (GTK_ENTRY (entry->priv->entry),
					 GTK_ENTRY_ICON_SECONDARY,
					 _("Clear the search text"));
	gtk_entry_set_icon_from_icon_name (GTK_ENTRY (entry->priv->entry),
					   GTK_ENTRY_ICON_PRIMARY,
					   "edit-find-symbolic");
	if (entry->priv->has_popup) {
		gtk_entry_set_icon_tooltip_text (GTK_ENTRY (entry->priv->entry),
						 GTK_ENTRY_ICON_PRIMARY,
						 _("Select the search type"));
	}

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
		gtk_widget_set_visible (entry->priv->button, entry->priv->explicit_mode == TRUE);
		rb_search_entry_update_icons (entry);
		break;
	case PROP_HAS_POPUP:
		entry->priv->has_popup = g_value_get_boolean (value);
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
	case PROP_HAS_POPUP:
		g_value_set_boolean (value, entry->priv->has_popup);
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
rb_search_entry_new (gboolean has_popup)
{
	RBSearchEntry *entry;

	entry = RB_SEARCH_ENTRY (g_object_new (RB_TYPE_SEARCH_ENTRY,
					       "spacing", 5,
					       "has-popup", has_popup,
					       "hexpand", TRUE,
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

/**
 * rb_search_entry_set_placeholder:
 * @entry: a #RBSearchEntry
 * @text: placeholder text
 *
 * Sets the placeholder text in the search entry box.
 */
void
rb_search_entry_set_placeholder (RBSearchEntry *entry, const char *text)
{
	gtk_entry_set_placeholder_text (GTK_ENTRY (entry->priv->entry), text);
}

static void
rb_search_entry_update_icons (RBSearchEntry *entry)
{
	const char *text;
	const char *icon;

	icon = NULL;
	text = gtk_entry_get_text (GTK_ENTRY (entry->priv->entry));
	if (text && *text) {
		icon = "edit-clear-symbolic";
	}
	gtk_entry_set_icon_from_icon_name (GTK_ENTRY (entry->priv->entry),
					   GTK_ENTRY_ICON_SECONDARY,
					   icon);
}

static void
rb_search_entry_changed_cb (GtkEditable *editable,
			    RBSearchEntry *entry)
{
	const char *text;

	if (entry->priv->clearing == TRUE) {
		entry->priv->searching = FALSE;
		rb_search_entry_update_icons (entry);
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
	rb_search_entry_update_icons (entry);
}

static gboolean
rb_search_entry_timeout_cb (RBSearchEntry *entry)
{
	const char *text;

	text = gtk_entry_get_text (GTK_ENTRY (entry->priv->entry));

	if (entry->priv->explicit_mode == FALSE) {
		g_signal_emit (G_OBJECT (entry), rb_search_entry_signals[SEARCH], 0, text);
	}
	entry->priv->timeout = 0;

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
	rb_search_entry_update_icons (entry);
	g_signal_emit (G_OBJECT (entry), rb_search_entry_signals[ACTIVATE], 0,
		       gtk_entry_get_text (GTK_ENTRY (entry->priv->entry)));
}

static void
button_clicked_cb (GtkButton *button, RBSearchEntry *entry)
{
	entry->priv->searching = TRUE;
	rb_search_entry_update_icons (entry);
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
rb_search_entry_widget_grab_focus (GtkWidget *widget)
{
	rb_search_entry_grab_focus (RB_SEARCH_ENTRY (widget));
}

static void
rb_search_entry_clear_cb (GtkEntry *entry,
			  GtkEntryIconPosition icon_pos,
			  GdkEvent *event,
			  RBSearchEntry *search_entry)
{
	if (icon_pos == GTK_ENTRY_ICON_PRIMARY) {
		g_signal_emit (G_OBJECT (search_entry), rb_search_entry_signals[SHOW_POPUP], 0);
	} else {
		rb_search_entry_set_text (search_entry, "");
	}
}

/**
 * rb_search_entry_set_mnemonic:
 * @entry: a #RBSearchEntry
 * @enable: if %TRUE, enable the mnemonic
 *
 * Adds or removes a mnemonic allowing the user to focus
 * the search entry.
 */
void
rb_search_entry_set_mnemonic (RBSearchEntry *entry, gboolean enable)
{
	GtkWidget *toplevel;
	guint keyval;
	gunichar accel = 0;

	if (pango_parse_markup (_("_Search:"), -1, '_', NULL, NULL, &accel, NULL) && accel != 0) {
		keyval = gdk_keyval_to_lower (gdk_unicode_to_keyval (accel));
	} else {
		keyval = gdk_unicode_to_keyval ('s');
	}

	toplevel = gtk_widget_get_toplevel (GTK_WIDGET (entry));
	if (gtk_widget_is_toplevel (toplevel)) {
		if (enable) {
			gtk_window_add_mnemonic (GTK_WINDOW (toplevel), keyval, entry->priv->entry);
		} else {
			gtk_window_remove_mnemonic (GTK_WINDOW (toplevel), keyval, entry->priv->entry);
		}
	}
}
