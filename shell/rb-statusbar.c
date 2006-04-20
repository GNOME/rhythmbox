/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 *  arch-tag: Implementation of status display widget
 *
 *  Copyright (C) 2003 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003,2004 Colin Walters <walters@redhat.com>
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

#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "rb-statusbar.h"
#include "rb-debug.h"

#define EPSILON		(0.00001)

static GObject* rb_statusbar_construct (GType type,
					guint n_construct_properties,
					GObjectConstructParam *construct_properties);
static void rb_statusbar_class_init (RBStatusbarClass *klass);
static void rb_statusbar_init (RBStatusbar *statusbar);
static void rb_statusbar_finalize (GObject *object);
static void rb_statusbar_set_property (GObject *object,
				       guint prop_id,
				       const GValue *value,
				       GParamSpec *pspec);
static void rb_statusbar_get_property (GObject *object,
				       guint prop_id,
				       GValue *value,
				       GParamSpec *pspec);
static void rb_statusbar_sync_with_source (RBStatusbar *statusbar);

static void rb_statusbar_sync_status (RBStatusbar *status);
static gboolean poll_status (RBStatusbar *status);
static void rb_statusbar_source_status_changed_cb (RBSource *source,
						   RBStatusbar *statusbar);

struct RBStatusbarPrivate
{
        RBSource *selected_source;

        RhythmDB *db;

        GtkActionGroup *actiongroup;
        GtkTooltips *tooltips;

        GtkWidget *progress;
        double progress_fraction;
        gboolean progress_changed;
        gchar *progress_text;

        gchar *loading_text;

        guint status_poll_id;

        gboolean idle;
        guint idle_tick_id;
};

#define RB_STATUSBAR_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_STATUSBAR, RBStatusbarPrivate))

enum
{
        PROP_0,
        PROP_DB,
        PROP_ACTION_GROUP,
        PROP_SOURCE
};

G_DEFINE_TYPE (RBStatusbar, rb_statusbar, GTK_TYPE_STATUSBAR)

static void
rb_statusbar_class_init (RBStatusbarClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->constructor = rb_statusbar_construct;
        object_class->finalize = rb_statusbar_finalize;

        object_class->set_property = rb_statusbar_set_property;
        object_class->get_property = rb_statusbar_get_property;

        g_object_class_install_property (object_class,
                                         PROP_DB,
                                         g_param_spec_object ("db",
                                                              "RhythmDB",
                                                              "RhythmDB object",
                                                              RHYTHMDB_TYPE,
                                                              G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_SOURCE,
                                         g_param_spec_object ("source",
                                                              "RBSource",
                                                              "RBSource object",
                                                              RB_TYPE_SOURCE,
                                                              G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_ACTION_GROUP,
                                         g_param_spec_object ("action-group",
                                                              "GtkActionGroup",
                                                              "GtkActionGroup object",
                                                              GTK_TYPE_ACTION_GROUP,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (RBStatusbarPrivate));
}

static GObject*
rb_statusbar_construct (GType                  type,
                        guint                  n_construct_properties,
                        GObjectConstructParam *construct_properties)
{
        RBStatusbarClass *klass;
        GObject *object;
        RBStatusbar *statusbar;

        klass = RB_STATUSBAR_CLASS (g_type_class_peek (RB_TYPE_STATUSBAR));
        object = G_OBJECT_CLASS (rb_statusbar_parent_class)->constructor
					(type,
					 n_construct_properties,
					 construct_properties);
        
        statusbar = RB_STATUSBAR (object);

        return object;
}


static void
rb_statusbar_init (RBStatusbar *statusbar)
{
	statusbar->priv = RB_STATUSBAR_GET_PRIVATE (statusbar);

        statusbar->priv->tooltips = gtk_tooltips_new ();
        gtk_tooltips_enable (statusbar->priv->tooltips);

        gtk_statusbar_set_has_resize_grip (GTK_STATUSBAR (statusbar), TRUE);

        statusbar->priv->progress = gtk_progress_bar_new ();
        statusbar->priv->progress_fraction = 1.0;

        statusbar->priv->loading_text = g_strdup (_("Loading..."));

        gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (statusbar->priv->progress), 1.0);
        gtk_widget_hide (statusbar->priv->progress);

        gtk_box_pack_start (GTK_BOX (statusbar),
                            GTK_WIDGET (statusbar->priv->progress), FALSE, TRUE, 0);
}

static void
rb_statusbar_finalize (GObject *object)
{
        RBStatusbar *statusbar;

        g_return_if_fail (object != NULL);
        g_return_if_fail (RB_IS_STATUSBAR (object));

        statusbar = RB_STATUSBAR (object);

        g_return_if_fail (statusbar->priv != NULL);

	g_free (statusbar->priv->loading_text);
	g_free (statusbar->priv->progress_text);

        if (statusbar->priv->idle_tick_id) {
                g_source_remove (statusbar->priv->idle_tick_id);
                statusbar->priv->idle_tick_id = 0;
        }
        if (statusbar->priv->status_poll_id)
                g_source_remove (statusbar->priv->status_poll_id);
        
        G_OBJECT_CLASS (rb_statusbar_parent_class)->finalize (object);
}


typedef struct {
        GtkWidget *statusbar;
        char      *tooltip;
} StatusTip;

static void
statustip_free (StatusTip *tip)
{
        g_object_unref (tip->statusbar);
        g_free (tip->tooltip);
        g_free (tip);
}

static void
set_statusbar_tooltip (GtkWidget *widget,
                       StatusTip *data)
{
        guint context_id;

        context_id = gtk_statusbar_get_context_id (GTK_STATUSBAR (data->statusbar),
                                                   "rb_statusbar_tooltip");
        gtk_statusbar_push (GTK_STATUSBAR (data->statusbar),
                            context_id,
                            data->tooltip ? data->tooltip: "");
}

static void
unset_statusbar_tooltip (GtkWidget *widget,
                         GtkWidget *statusbar)
{
        guint context_id;

        context_id = gtk_statusbar_get_context_id (GTK_STATUSBAR (statusbar),
                                                   "rb_statusbar_tooltip");
        gtk_statusbar_pop (GTK_STATUSBAR (statusbar), context_id);
}

static void
rb_statusbar_connect_action_group (RBStatusbar    *statusbar,
                                   GtkAction      *action,
                                   GtkWidget      *proxy,
                                   GtkActionGroup *action_group)
{
        char *tooltip;

        if (! GTK_IS_MENU_ITEM (proxy))
                return;

        g_object_get (action, "tooltip", &tooltip, NULL);

        if (tooltip) {
                StatusTip *statustip;

                statustip = g_new (StatusTip, 1);
                statustip->statusbar = g_object_ref (statusbar);
                statustip->tooltip = tooltip;
                g_signal_connect_data (proxy, "select",
                                       G_CALLBACK (set_statusbar_tooltip),
                                       statustip, (GClosureNotify)statustip_free, 0);

                g_signal_connect (proxy, "deselect",
                                  G_CALLBACK (unset_statusbar_tooltip),
                                  statusbar);
        }
}

static void
rb_statusbar_set_property (GObject *object,
                           guint prop_id,
                           const GValue *value,
                           GParamSpec *pspec)
{
        RBStatusbar *statusbar = RB_STATUSBAR (object);

        switch (prop_id)
        {
        case PROP_DB:
                statusbar->priv->db = g_value_get_object (value);
                statusbar->priv->status_poll_id
                        = g_idle_add ((GSourceFunc) poll_status, statusbar);
                break;
        case PROP_SOURCE:
                if (statusbar->priv->selected_source != NULL) {
			g_signal_handlers_disconnect_by_func (G_OBJECT (statusbar->priv->selected_source),
							      G_CALLBACK (rb_statusbar_source_status_changed_cb),
							      statusbar);
                }
                
                statusbar->priv->selected_source = g_value_get_object (value);
                rb_debug ("selected source %p", g_value_get_object (value));

                if (statusbar->priv->selected_source != NULL) {
			g_signal_connect_object (G_OBJECT (statusbar->priv->selected_source),
						 "status-changed",
						 G_CALLBACK (rb_statusbar_source_status_changed_cb),
						 statusbar, 0);
                }
                rb_statusbar_sync_with_source (statusbar);

                break;
        case PROP_ACTION_GROUP:
                if (statusbar->priv->actiongroup) {
                        g_signal_handlers_disconnect_by_func (G_OBJECT (statusbar->priv->actiongroup),
                                                              G_CALLBACK (rb_statusbar_connect_action_group),
                                                              statusbar);
                }
                statusbar->priv->actiongroup = g_value_get_object (value);

                g_signal_connect_object (statusbar->priv->actiongroup,
                                         "connect-proxy",
                                         G_CALLBACK (rb_statusbar_connect_action_group),
                                         statusbar,
                                         G_CONNECT_SWAPPED);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void 
rb_statusbar_get_property (GObject *object,
                              guint prop_id,
                              GValue *value,
                              GParamSpec *pspec)
{
        RBStatusbar *statusbar = RB_STATUSBAR (object);

        switch (prop_id)
        {
        case PROP_DB:
                g_value_set_object (value, statusbar->priv->db);
                break;
        case PROP_SOURCE:
                g_value_set_object (value, statusbar->priv->selected_source);
                break;
        case PROP_ACTION_GROUP:
                g_value_set_object (value, statusbar->priv->actiongroup);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

void
rb_statusbar_set_source (RBStatusbar *statusbar,
                            RBSource *source)
{
        g_return_if_fail (RB_IS_STATUSBAR (statusbar));
        g_return_if_fail (RB_IS_SOURCE (source));

        g_object_set (G_OBJECT (statusbar),
                      "source", source,
                      NULL);
}

static gboolean
status_tick_cb (GtkProgressBar *progress)
{
        g_return_val_if_fail (GTK_IS_PROGRESS_BAR (progress), FALSE);

        GDK_THREADS_ENTER ();

        gtk_progress_bar_pulse (progress);

        GDK_THREADS_LEAVE ();

        return TRUE;
}

static void
rb_statusbar_sync_status (RBStatusbar *status)
{
        gboolean changed = FALSE;
        gboolean show_progress = TRUE;
        gboolean pulse_progress = FALSE;
        char *status_text = NULL;

        /*
         * Behaviour of status bar.
         * progress bar:
         * - if we have a progress fraction, display it
         * - otherwise, if the library or selected entry view are busy, pulse
         * - otherwise, hide.
         * 
         * status text:
         * - if we have a progress fraction, display its text
         * - otherwise, if the library is busy, display its text
         * - otherwise, display the selected source's status text
         */

        /* 1. progress bar moving? */
        if (status->priv->progress_fraction < (1.0 - EPSILON) ||
            status->priv->progress_changed) {
                status_text = status->priv->progress_text;
                status->priv->progress_changed = FALSE;
        }

        /* 2. library busy? */
        if (status_text == NULL && rhythmdb_is_busy (status->priv->db)) {
                status_text = status->priv->loading_text;
                pulse_progress = TRUE;
        }

        /* 3. query model busy? */
        if (status_text == NULL && status->priv->selected_source) {
		RhythmDBQueryModel *model;

		g_object_get (G_OBJECT (status->priv->selected_source), "query-model", &model, NULL);
                if (rhythmdb_query_model_has_pending_changes (model))
                        pulse_progress = TRUE;
                else
                        show_progress = FALSE;
		g_object_unref (G_OBJECT (model));
        }

        /* set up the status text */
        if (status_text) {
                gtk_statusbar_pop (GTK_STATUSBAR (status), 0);
                gtk_statusbar_push (GTK_STATUSBAR (status), 0, status_text);

                changed = TRUE;
                status->priv->idle = FALSE;
        } else if (!status->priv->idle) {
                rb_statusbar_sync_with_source (status);
                changed = TRUE;
                status->priv->idle = TRUE;
        }

        if (!pulse_progress && status->priv->idle_tick_id > 0) {
                g_source_remove (status->priv->idle_tick_id);
                status->priv->idle_tick_id = 0;
                changed = TRUE;
        }

        /* Sync the progress bar */
        if (show_progress) {
                gtk_widget_show (status->priv->progress);

                if (pulse_progress) {
                        if (status->priv->idle_tick_id == 0) {
                                status->priv->idle_tick_id
                                        = g_timeout_add (250, (GSourceFunc) status_tick_cb,
                                                         status->priv->progress);
                        }
                        changed = TRUE;
                } else {
                        gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (status->priv->progress),
                                                       status->priv->progress_fraction);
                }
        } else {
                gtk_widget_hide (status->priv->progress);
        }

        if (status->priv->status_poll_id == 0)
                status->priv->status_poll_id = 
                        g_timeout_add (changed ? 350 : 750, (GSourceFunc) poll_status, status);
}

static gboolean
poll_status (RBStatusbar *status)
{
        GDK_THREADS_ENTER ();

        status->priv->status_poll_id = 0;
        rb_statusbar_sync_status (status);

        GDK_THREADS_LEAVE ();

        return FALSE;
}

RBStatusbar *
rb_statusbar_new (RhythmDB *db, GtkActionGroup *actions)
{
        RBStatusbar *statusbar = g_object_new (RB_TYPE_STATUSBAR,
                                               "db", db,
                                               "action-group", actions,
                                               NULL);

        g_return_val_if_fail (statusbar->priv != NULL, NULL);

        return statusbar;
}

void
rb_statusbar_set_progress (RBStatusbar *statusbar, double progress, const char *text)
{
        if (statusbar->priv->progress_text) {
                g_free (statusbar->priv->progress_text);
                statusbar->priv->progress_text = NULL;
        }
        
        if (progress >= 0.0) {
                statusbar->priv->progress_fraction = progress;
                statusbar->priv->progress_changed = TRUE;
                statusbar->priv->progress_text = g_strdup (text);
        } else {
                /* trick sync_status into hiding it */
                statusbar->priv->progress_fraction = 1.0;
                statusbar->priv->progress_changed = FALSE;
        }
        rb_statusbar_sync_status (statusbar);
}

static void
rb_statusbar_sync_with_source (RBStatusbar *statusbar)
{
	char *status_str;

	status_str = rb_source_get_status (statusbar->priv->selected_source);
        gtk_statusbar_pop (GTK_STATUSBAR (statusbar), 0);
        gtk_statusbar_push (GTK_STATUSBAR (statusbar), 0, status_str);
	g_free (status_str);
}

static void
rb_statusbar_source_status_changed_cb (RBSource *source, RBStatusbar *statusbar)
{
	rb_debug ("source status changed");
	if (statusbar->priv->idle)
		rb_statusbar_sync_with_source (statusbar);
}
