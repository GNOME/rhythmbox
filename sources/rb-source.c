/*
 *  arch-tag: Implementation of the abstract base class of all sources
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003 Colin Walters <walters@gnome.org>
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

#include <config.h>
#include <bonobo/bonobo-ui-component.h>
#include <bonobo/bonobo-ui-util.h>
#include <libgnome/gnome-i18n.h>
#include <time.h>

#include "rb-source.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#include "rb-cut-and-paste-code.h"
#include "rb-bonobo-helpers.h"

static void rb_source_class_init (RBSourceClass *klass);
static void rb_source_init (RBSource *source);
static void rb_source_finalize (GObject *object);
static void rb_source_set_property (GObject *object,
					guint prop_id,
					const GValue *value,
					GParamSpec *pspec);
static void rb_source_get_property (GObject *object,
					guint prop_id,
					GValue *value,
					GParamSpec *pspec);

static const char * default_get_browser_key (RBSource *status);
static GList *default_get_extra_views (RBSource *source);
static gboolean default_can_rename (RBSource *source);
static gboolean default_can_search (RBSource *source);
static gboolean default_can_cut (RBSource *source);
static GList *default_copy (RBSource *source);
static void default_reset_filters (RBSource *source);
static void default_song_properties (RBSource *source);
static GtkWidget * default_get_config_widget (RBSource *source);
static RBSourceEOFType default_handle_eos (RBSource *source);
static void default_buffering_done  (RBSource *source);
static gboolean default_receive_drag  (RBSource *source, GtkSelectionData *data);
static gboolean default_show_popup  (RBSource *source);
static void default_delete_thyself (RBSource *source);

struct RBSourcePrivate
{
	char *name;
	
	BonoboUIComponent *component;
};

enum
{
	PROP_0,
	PROP_NAME,
};

enum
{
	STATUS_CHANGED,
	FILTER_CHANGED,
	DELETED,
	LAST_SIGNAL
};

static guint rb_source_signals[LAST_SIGNAL] = { 0 };

static GObjectClass *parent_class = NULL;

GType
rb_source_get_type (void)
{
	static GType rb_source_type = 0;

	if (rb_source_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBSourceClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_source_class_init,
			NULL,
			NULL,
			sizeof (RBSource),
			0,
			(GInstanceInitFunc) rb_source_init
		};

		rb_source_type = g_type_register_static (GTK_TYPE_HBOX,
						       "RBSource",
						       &our_info, G_TYPE_FLAG_ABSTRACT);
	}

	return rb_source_type;
}

static void
rb_source_class_init (RBSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_source_finalize;

	object_class->set_property = rb_source_set_property;
	object_class->get_property = rb_source_get_property;

	klass->impl_get_browser_key = default_get_browser_key;
	klass->impl_get_extra_views = default_get_extra_views;
	klass->impl_can_rename = default_can_rename;
	klass->impl_can_search = default_can_search;
	klass->impl_can_cut = default_can_cut;
	klass->impl_can_delete = default_can_cut;
	klass->impl_can_copy = default_can_cut;
	klass->impl_copy = default_copy;
	klass->impl_reset_filters = default_reset_filters;
	klass->impl_song_properties = default_song_properties;
	klass->impl_handle_eos = default_handle_eos;
	klass->impl_buffering_done = default_buffering_done;
	klass->impl_get_config_widget = default_get_config_widget;
	klass->impl_receive_drag = default_receive_drag;
	klass->impl_show_popup = default_show_popup;
	klass->impl_delete_thyself = default_delete_thyself;

	g_object_class_install_property (object_class,
					 PROP_NAME,
					 g_param_spec_string ("name",
							      "UI name",
							      "Interface name",
							      NULL,
							      G_PARAM_READWRITE));
	rb_source_signals[DELETED] =
		g_signal_new ("deleted",
			      RB_TYPE_SOURCE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBSourceClass, status_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);

	rb_source_signals[STATUS_CHANGED] =
		g_signal_new ("status_changed",
			      RB_TYPE_SOURCE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBSourceClass, status_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);

	/**
	 * Fires when the user changes the filter, either by changing the
	 * contents of the search box or by selecting a different browser
	 * entry.
	 */
	rb_source_signals[FILTER_CHANGED] =
		g_signal_new ("filter_changed",
			      RB_TYPE_SOURCE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBSourceClass, filter_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
}

static void
rb_source_init (RBSource *source)
{
	source->priv = g_new0 (RBSourcePrivate, 1);

}

static void
rb_source_finalize (GObject *object)
{
	RBSource *source;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SOURCE (object));

	source = RB_SOURCE (object);

	g_return_if_fail (source->priv != NULL);

	rb_debug ("Finalizing view %p", source);

	g_free (source->priv->name);

	g_free (source->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_source_set_property (GObject *object,
		      guint prop_id,
		      const GValue *value,
		      GParamSpec *pspec)
{
	RBSource *source = RB_SOURCE (object);

	switch (prop_id)
	{
	case PROP_NAME:
		source->priv->name = g_strdup (g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void 
rb_source_get_property (GObject *object,
		      guint prop_id,
		      GValue *value,
		      GParamSpec *pspec)
{
	RBSource *source = RB_SOURCE (object);

	switch (prop_id)
	{
	case PROP_NAME:
		g_value_set_string (value, source->priv->name);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

const char *
rb_source_get_status (RBSource *status)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (status);

	return klass->impl_get_status (status);
}

static const char *
default_get_browser_key (RBSource *status)
{
	return NULL;
}

const char *
rb_source_get_browser_key (RBSource *status)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (status);

	return klass->impl_get_browser_key (status);
}

void
rb_source_notify_status_changed (RBSource *status)
{
	g_signal_emit (G_OBJECT (status), rb_source_signals[STATUS_CHANGED], 0);
}

void
rb_source_notify_filter_changed (RBSource *status)
{
	g_signal_emit (G_OBJECT (status), rb_source_signals[FILTER_CHANGED], 0);
}

void
rb_source_update_play_statistics (RBSource *source, RhythmDB *db, RhythmDBEntry *entry)
{
	time_t now;
	guint current_count;
	GValue value = { 0, };

	g_value_init (&value, G_TYPE_INT);

	rhythmdb_read_lock (db);
	current_count = rhythmdb_entry_get_int (db, entry, RHYTHMDB_PROP_PLAY_COUNT);
	rhythmdb_read_unlock (db);

	g_value_set_int (&value, current_count + 1);

	/* Increment current play count */
	rhythmdb_entry_queue_set (db, entry, RHYTHMDB_PROP_PLAY_COUNT, &value);
	g_value_unset (&value);
	
	/* Reset the last played time */
	time (&now);

	g_value_init (&value, G_TYPE_LONG);
	g_value_set_long (&value, now);
	rhythmdb_entry_queue_set (db, entry, RHYTHMDB_PROP_LAST_PLAYED, &value);
	g_value_unset (&value);
}

RBEntryView *
rb_source_get_entry_view (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_get_entry_view (source);
}

static GList *
default_get_extra_views (RBSource *source)
{
	return NULL;
}

GList *
rb_source_get_extra_views (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_get_extra_views (source);
}

GdkPixbuf *
rb_source_get_pixbuf (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_get_pixbuf (source);
}

static gboolean
default_can_rename (RBSource *source)
{
	return FALSE;
}

gboolean
rb_source_can_rename (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_can_rename (source);
}

static gboolean
default_can_search (RBSource *source)
{
	return FALSE;
}

gboolean
rb_source_can_search (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_can_search (source);
}

void
rb_source_search (RBSource *source, const char *text)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	klass->impl_search (source, text);
}

static GtkWidget *
default_get_config_widget (RBSource *source)
{
	return NULL;
}

GtkWidget *
rb_source_get_config_widget (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_get_config_widget (source);
}

static gboolean
default_can_cut (RBSource *source)
{
	return FALSE;
}

gboolean
rb_source_can_cut (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_can_cut (source);
}

gboolean
rb_source_can_delete (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_can_delete (source);
}

gboolean
rb_source_can_copy (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_can_copy (source);
}

GList *
rb_source_cut (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_cut (source);
}

static GList *
default_copy (RBSource *source)
{
	return rb_entry_view_get_selected_entries (rb_source_get_entry_view (source));
}
	
GList *
rb_source_copy (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_copy (source);
}

void
rb_source_paste (RBSource *source, GList *nodes)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	klass->impl_paste (source, nodes);
}

void
rb_source_delete (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	klass->impl_delete (source);
}

static void
default_reset_filters (RBSource *source)
{
	rb_debug ("no implementation of reset_filters for this source");
}

void
rb_source_reset_filters (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	klass->impl_reset_filters (source);
}

static void
default_song_properties (RBSource *source)
{
	rb_error_dialog (_("No properties available."));
}

void
rb_source_song_properties (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	klass->impl_song_properties (source);
}

gboolean
rb_source_can_pause (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_can_pause (source);
}

static RBSourceEOFType
default_handle_eos (RBSource *source)
{
	return RB_SOURCE_EOF_NEXT;
}

RBSourceEOFType
rb_source_handle_eos (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_handle_eos (source);
}

gboolean
rb_source_have_artist_album (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_have_artist_album (source);
}

const char *
rb_source_get_artist (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_get_artist (source);
}

const char *
rb_source_get_album (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_get_album (source);
}

gboolean
rb_source_have_url (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_have_url (source);
}

static void
default_buffering_done  (RBSource *source)
{
	rb_debug ("No implementation of buffering_done for active source");
}
	

void
rb_source_buffering_done (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	klass->impl_buffering_done (source);
}

gboolean
default_receive_drag (RBSource *source, GtkSelectionData *data)
{
	rb_error_dialog (_("This source does not support drag and drop."));
	return FALSE;
}

gboolean
rb_source_receive_drag (RBSource *source, GtkSelectionData *data)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_receive_drag (source, data);
}

static gboolean
default_show_popup  (RBSource *source)
{
	return FALSE;
}

gboolean
rb_source_show_popup (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_show_popup (source);
}


static void
default_delete_thyself (RBSource *source)
{
}

void
rb_source_delete_thyself (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	klass->impl_delete_thyself (source);
	g_signal_emit (G_OBJECT (source), rb_source_signals[DELETED], 0);
}
