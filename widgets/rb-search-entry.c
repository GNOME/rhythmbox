/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  arch-tag: Implementation of search entry widget
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#include <config.h>

#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include "libsexy/sexy-icon-entry.h"

#include "rb-search-entry.h"

static void rb_search_entry_class_init (RBSearchEntryClass *klass);
static void rb_search_entry_init (RBSearchEntry *entry);
static void rb_search_entry_finalize (GObject *object);
static gboolean rb_search_entry_timeout_cb (RBSearchEntry *entry);
static void rb_search_entry_changed_cb (GtkEditable *editable,
			                RBSearchEntry *entry);
static void rb_search_entry_activate_cb (GtkEntry *gtkentry,
					 RBSearchEntry *entry);
static gboolean rb_search_entry_focus_out_event_cb (GtkWidget *widget,
				                    GdkEventFocus *event,
				                    RBSearchEntry *entry);

struct RBSearchEntryPrivate
{
	GtkWidget *entry;

	gboolean clearing;

	guint timeout;
};

G_DEFINE_TYPE (RBSearchEntry, rb_search_entry, GTK_TYPE_HBOX)
#define RB_SEARCH_ENTRY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_SEARCH_ENTRY, RBSearchEntryPrivate))

enum
{
	SEARCH,
	ACTIVATE,
	LAST_SIGNAL
};

static guint rb_search_entry_signals[LAST_SIGNAL] = { 0 };


static void
rb_search_entry_class_init (RBSearchEntryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = rb_search_entry_finalize;

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
	rb_search_entry_signals[ACTIVATE] =
		g_signal_new ("activate",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBSearchEntryClass, activate),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);

	g_type_class_add_private (klass, sizeof (RBSearchEntryPrivate));
}

static void
rb_search_entry_init (RBSearchEntry *entry)
{
	GtkWidget *label;

	entry->priv = RB_SEARCH_ENTRY_GET_PRIVATE (entry);

	/* this string can only be so long, or there wont be a search entry :) */
	label = gtk_label_new_with_mnemonic (_("_Search:"));
	gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_RIGHT);
	gtk_box_pack_start (GTK_BOX (entry), label, FALSE, TRUE, 0);

	entry->priv->entry = sexy_icon_entry_new ();
	sexy_icon_entry_add_clear_button (SEXY_ICON_ENTRY (entry->priv->entry));

	gtk_label_set_mnemonic_widget (GTK_LABEL (label),
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

void
rb_search_entry_set_text (RBSearchEntry *entry, const char *text)
{
	gtk_entry_set_text (GTK_ENTRY (entry->priv->entry),
			    text ? text : "");
}

static void
rb_search_entry_changed_cb (GtkEditable *editable,
			    RBSearchEntry *entry)
{
	if (entry->priv->clearing == TRUE)
		return;

	if (entry->priv->timeout != 0) {
		g_source_remove (entry->priv->timeout);
		entry->priv->timeout = 0;
	}

	/* emit it now if we're clearing the entry */
	if (gtk_entry_get_text (GTK_ENTRY (entry->priv->entry)))
		entry->priv->timeout = g_timeout_add (300, (GSourceFunc) rb_search_entry_timeout_cb, entry);
	else
		rb_search_entry_timeout_cb (entry);
}

static gboolean
rb_search_entry_timeout_cb (RBSearchEntry *entry)
{
	gdk_threads_enter ();
	
	g_signal_emit (G_OBJECT (entry), rb_search_entry_signals[SEARCH], 0,
		       gtk_entry_get_text (GTK_ENTRY (entry->priv->entry)));
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

	g_signal_emit (G_OBJECT (entry), rb_search_entry_signals[SEARCH], 0,
		       gtk_entry_get_text (GTK_ENTRY (entry->priv->entry)));

	return FALSE;
}

gboolean
rb_search_entry_searching(RBSearchEntry *entry)
{
	return strcmp ("", gtk_entry_get_text (GTK_ENTRY (entry->priv->entry))) != 0;
}

static void
rb_search_entry_activate_cb (GtkEntry *gtkentry,
			     RBSearchEntry *entry)
{
	g_signal_emit (G_OBJECT (entry), rb_search_entry_signals[ACTIVATE], 0);
}

void
rb_search_entry_grab_focus (RBSearchEntry *entry)
{
	gtk_widget_grab_focus (GTK_WIDGET (entry->priv->entry));
}
