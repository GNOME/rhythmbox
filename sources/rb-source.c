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
#include <libgnome/gnome-i18n.h>
#include <gtk/gtkuimanager.h>
#include <time.h>


#include "rb-cut-and-paste-code.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#include "rb-shell.h"
#include "rb-source.h"
#include "rb-util.h"

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
static GdkPixbuf *default_get_pixbuf (RBSource *source);
static GList *default_copy (RBSource *source);
static void default_reset_filters (RBSource *source);
static void default_song_properties (RBSource *source);
static GtkWidget * default_get_config_widget (RBSource *source);
static gboolean default_try_playlist (RBSource *source);
static RBSourceEOFType default_handle_eos (RBSource *source);
static gboolean default_receive_drag  (RBSource *source, GtkSelectionData *data);
static gboolean default_show_popup  (RBSource *source);
static void default_delete_thyself (RBSource *source);
static void default_activate (RBSource *source);
static void default_deactivate (RBSource *source);
static gboolean default_disconnect (RBSource *source);

G_DEFINE_ABSTRACT_TYPE (RBSource, rb_source, GTK_TYPE_HBOX)
#define RB_SOURCE_GET_PRIVATE(object)(G_TYPE_INSTANCE_GET_PRIVATE ((object), RB_TYPE_SOURCE, RBSourcePrivate))

struct _RBSourcePrivate
{
	char *name;
	
	RBShell *shell;
	gboolean visible;
};

enum
{
	PROP_0,
	PROP_NAME,
	PROP_SHELL,
	PROP_UI_MANAGER,
	PROP_VISIBLE
};

enum
{
	STATUS_CHANGED,
	FILTER_CHANGED,
	DELETED,
	LAST_SIGNAL
};

static guint rb_source_signals[LAST_SIGNAL] = { 0 };

static void
rb_source_class_init (RBSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

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
	klass->impl_get_pixbuf = default_get_pixbuf;
	klass->impl_copy = default_copy;
	klass->impl_reset_filters = default_reset_filters;
	klass->impl_song_properties = default_song_properties;
	klass->impl_handle_eos = default_handle_eos;
	klass->impl_get_config_widget = default_get_config_widget;
	klass->impl_receive_drag = default_receive_drag;
	klass->impl_show_popup = default_show_popup;
	klass->impl_delete_thyself = default_delete_thyself;
	klass->impl_activate = default_activate;
	klass->impl_deactivate = default_deactivate;
	klass->impl_disconnect = default_disconnect;
	klass->impl_try_playlist = default_try_playlist;

	g_object_class_install_property (object_class,
					 PROP_NAME,
					 g_param_spec_string ("name",
							      "UI name",
							      "Interface name",
							      NULL,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class,
					 PROP_SHELL,
					 g_param_spec_object ("shell",
							       "RBShell",
							       "RBShell object",
							      RB_TYPE_SHELL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_UI_MANAGER,
					 g_param_spec_object ("ui-manager",
							       "GtkUIManager",
							       "GtkUIManager object",
							      GTK_TYPE_UI_MANAGER,
							      G_PARAM_READABLE));


	g_object_class_install_property (object_class, 
					 PROP_VISIBLE,
					 /* FIXME: This property could probably
					  * be named better, there's already
					  * a GtkWidget 'visible' property,
					  * since RBSource derives from
					  * GtkWidget, this can be confusing
					  */
					 g_param_spec_boolean ("visibility", 
							       "visibility",
							       "Whether the source should be displayed in the source list",
							       TRUE,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

	rb_source_signals[DELETED] =
		g_signal_new ("deleted",
			      RB_TYPE_SOURCE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBSourceClass, deleted),
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

	/*
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
	
	g_type_class_add_private (object_class, sizeof (RBSourcePrivate));
}

static void
rb_source_init (RBSource *source)
{
	RB_SOURCE_GET_PRIVATE (source)->visible = TRUE;
}

static void
rb_source_finalize (GObject *object)
{
	RBSource *source;
	RBSourcePrivate *priv;
	
	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SOURCE (object));

	source = RB_SOURCE (object);
	priv = RB_SOURCE_GET_PRIVATE (source);
	g_return_if_fail (priv != NULL);

	rb_debug ("Finalizing view %p", source);

	g_free (priv->name);

	G_OBJECT_CLASS (rb_source_parent_class)->finalize (object);
}

static void
rb_source_set_property (GObject *object,
		      guint prop_id,
		      const GValue *value,
		      GParamSpec *pspec)
{
	RBSourcePrivate *priv = RB_SOURCE_GET_PRIVATE (object);

	switch (prop_id)
	{
	case PROP_NAME:
		g_free (priv->name);
		priv->name = g_strdup (g_value_get_string (value));
		break;
	case PROP_SHELL:
		priv->shell = g_value_get_object (value);
		break;
	case PROP_VISIBLE:
		priv->visible = g_value_get_boolean (value);
		rb_debug ("Setting %s visibility to %u", 
			  priv->name, 
			  priv->visible);
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
	RBSourcePrivate *priv = RB_SOURCE_GET_PRIVATE (object);

	switch (prop_id)
	{
	case PROP_NAME:
		g_value_set_string (value, priv->name);
		break;
	case PROP_SHELL:
		g_value_set_object (value, priv->shell);
		break;
	case PROP_VISIBLE:
		g_value_set_boolean (value, priv->visible);
		break;
	case PROP_UI_MANAGER:
	{
		GtkUIManager *manager;
		g_object_get (G_OBJECT (priv->shell), 
			      "ui-manager", &manager,
			      NULL);
		g_value_set_object (value, manager);
		g_object_unref (G_OBJECT(manager));
		break;
	}
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * rb_source_get_status:
 * @status: a #RBSource
 *
 * FIXME:
 * Some Random comments
 *
 * Returns: The status string
 **/
char *
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
	gulong current_count;
	GValue value = { 0, };

	g_value_init (&value, G_TYPE_ULONG);

	current_count = entry->play_count;

	g_value_set_ulong (&value, current_count + 1);

	/* Increment current play count */
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_PLAY_COUNT, &value);
	g_value_unset (&value);
	
	/* Reset the last played time */
	time (&now);

	g_value_init (&value, G_TYPE_ULONG);
	g_value_set_ulong (&value, now);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_LAST_PLAYED, &value);
	g_value_unset (&value);

	rhythmdb_commit (db);
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

static GdkPixbuf *
default_get_pixbuf (RBSource *source)
{
	return NULL;
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

	/* several sources don't have a search ability */
	if (klass->impl_search != NULL)
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

	if (klass->impl_get_config_widget) {
		return klass->impl_get_config_widget (source);
	} else {
		return NULL;
	}
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
	g_assert_not_reached ();
}

void
rb_source_song_properties (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	klass->impl_song_properties (source);
}

gboolean
rb_source_try_playlist (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_try_playlist (source);
}

gboolean
rb_source_can_pause (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_can_pause (source);
}

static gboolean
default_try_playlist (RBSource *source)
{
	return FALSE;
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
rb_source_have_url (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_have_url (source);
}

gboolean
default_receive_drag (RBSource *source, GtkSelectionData *data)
{
	rb_error_dialog (NULL,
			 _("Not supported"),
			 _("This source does not support drag and drop."));
	return FALSE;
}

gboolean
rb_source_receive_drag (RBSource *source, GtkSelectionData *data)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_receive_drag (source, data);
}

void
_rb_source_show_popup (RBSource *source, const char *ui_path)
{
	GtkUIManager *uimanager;

	g_object_get (G_OBJECT (RB_SOURCE_GET_PRIVATE (source)->shell), 
		      "ui-manager", &uimanager, NULL);
	rb_gtk_action_popup_menu (uimanager, ui_path);
	g_object_unref (G_OBJECT (uimanager));

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

static void default_activate (RBSource *source)
{
	return;
}

static void default_deactivate (RBSource *source)
{
	return;
}

static gboolean default_disconnect (RBSource *source)
{
	return TRUE;
}

void rb_source_activate (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	klass->impl_activate (source);

	return;
}

void rb_source_deactivate (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	klass->impl_deactivate (source);

	return;
}

gboolean rb_source_disconnect (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_disconnect (source);
}

