/*
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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include <gtk/gtk.h>
#include <config.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <libgnome/gnome-i18n.h>

#include "rb-statusbar.h"
#include "rb-preferences.h"
#include "rb-search-entry.h"
#include "rb-debug.h"
#include "eel-gconf-extensions.h"
#include "rb-play-order.h"

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
static void rb_statusbar_shell_play_order_changed_cb (RBShellPlayer *player, RBStatusbar *statusbar);
static void rb_statusbar_state_changed_cb (GConfClient *client,
					   guint cnxn_id,
					   GConfEntry *entry,
					   RBStatusbar *statusbar);

static void rb_statusbar_sync_status (RBStatusbar *status);
static gboolean poll_status (RBStatusbar *status);
static void rb_statusbar_toggle_changed_cb (GtkToggleButton *toggle,
					    RBStatusbar *statusbar);
static void rb_statusbar_view_statusbar_changed_cb (GtkAction *action,
						    RBStatusbar *statusbar);
static void rb_statusbar_entry_view_changed_cb (RBEntryView *view,
						RBStatusbar *statusbar);

static GtkToggleActionEntry rb_statusbar_toggle_entries [] =
{
        { "ViewStatusbar", NULL, N_("S_tatusbar"), NULL,
	  N_("Change the visibility of the statusbar"),
	  G_CALLBACK (rb_statusbar_view_statusbar_changed_cb) }
};
static guint rb_statusbar_n_toggle_entries = G_N_ELEMENTS (rb_statusbar_toggle_entries);

struct RBStatusbarPrivate
{
        RBSource *selected_source;

        RhythmDB *db;

	gboolean syncing_state;

        GtkActionGroup *actiongroup;
        RBShellPlayer *player;

        GtkTooltips *tooltips;

        GtkWidget *shuffle;
        GtkWidget *repeat;
        GtkWidget *status;

        GtkWidget *progress;

        guint status_poll_id;

        gboolean entry_view_busy;
        gboolean library_busy;
        gboolean buffering;
        guint idle_tick_id;

	guint notify_id;
};

enum
{
        PROP_0,
        PROP_DB,
        PROP_ACTION_GROUP,
        PROP_SOURCE,
        PROP_PLAYER
};

static GObjectClass *parent_class = NULL;

GType
rb_statusbar_get_type (void)
{
        static GType rb_statusbar_type = 0;

        if (rb_statusbar_type == 0)
        {
                static const GTypeInfo our_info =
                {
                        sizeof (RBStatusbarClass),
                        NULL,
                        NULL,
                        (GClassInitFunc) rb_statusbar_class_init,
                        NULL,
                        NULL,
                        sizeof (RBStatusbar),
                        0,
                        (GInstanceInitFunc) rb_statusbar_init
                };

                rb_statusbar_type = g_type_register_static (GTK_TYPE_HBOX,
                                                                "RBStatusbar",
                                                                &our_info, 0);
        }

        return rb_statusbar_type;
}

static void
rb_statusbar_class_init (RBStatusbarClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        parent_class = g_type_class_peek_parent (klass);

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

        g_object_class_install_property (object_class,
                                         PROP_PLAYER,
                                         g_param_spec_object ("player",
                                                              "RbShellPlayer",
                                                              "RbShellPlayer object",
                                                              RB_TYPE_SHELL_PLAYER,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));


}

static GObject*
rb_statusbar_construct (GType                  type,
                        guint                  n_construct_properties,
                        GObjectConstructParam *construct_properties)
{
        RBStatusbarClass *klass;
        GObjectClass *parent_class;  
        GObject *object;
        RBStatusbar *statusbar;

        klass = RB_STATUSBAR_CLASS (g_type_class_peek (RB_TYPE_STATUSBAR));
        parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (klass));
        object = parent_class->constructor (type,
                                            n_construct_properties,
                                            construct_properties);
        
        statusbar = RB_STATUSBAR (object);

	rb_statusbar_sync_state (statusbar);

        g_signal_connect_object (G_OBJECT (statusbar->priv->player),
				 "notify::play-order", 
				 G_CALLBACK (rb_statusbar_shell_play_order_changed_cb), 
				 statusbar, 0);
        g_signal_connect_swapped (G_OBJECT (statusbar->priv->player),
                                  "notify::buffering", 
                                  G_CALLBACK (rb_statusbar_sync_status), 
                                  statusbar);

        return object;
}


static void
rb_statusbar_init (RBStatusbar *statusbar)
{
        statusbar->priv = g_new0 (RBStatusbarPrivate, 1);

        statusbar->priv->tooltips = gtk_tooltips_new ();
        gtk_tooltips_enable (statusbar->priv->tooltips);

        statusbar->priv->shuffle = gtk_check_button_new_with_mnemonic (_("Sh_uffle"));
        gtk_tooltips_set_tip (GTK_TOOLTIPS (statusbar->priv->tooltips), 
                              GTK_WIDGET (statusbar->priv->shuffle), 
                              _("Play songs in a random order"), NULL);

        statusbar->priv->repeat = gtk_check_button_new_with_mnemonic (_("_Repeat"));
        gtk_tooltips_set_tip (GTK_TOOLTIPS (statusbar->priv->tooltips), 
                              GTK_WIDGET (statusbar->priv->repeat), 
                              _("Play first song again after all songs are played"), NULL);
        g_signal_connect_object (G_OBJECT (statusbar->priv->shuffle), "toggled",
                                 G_CALLBACK (rb_statusbar_toggle_changed_cb), statusbar, 0);
        g_signal_connect_object (G_OBJECT (statusbar->priv->repeat), "toggled",
                                 G_CALLBACK (rb_statusbar_toggle_changed_cb), statusbar, 0);

        statusbar->priv->status = gtk_label_new ("");
        gtk_label_set_use_markup (GTK_LABEL (statusbar->priv->status), TRUE);
        gtk_misc_set_alignment (GTK_MISC (statusbar->priv->status), 1.0, 0.5);

        gtk_box_set_spacing (GTK_BOX (statusbar), 5);

        statusbar->priv->progress = gtk_progress_bar_new ();

        gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (statusbar->priv->progress), 1.0);

        gtk_box_pack_start (GTK_BOX (statusbar),
                            GTK_WIDGET (statusbar->priv->shuffle), FALSE, TRUE, 0);
        gtk_box_pack_start (GTK_BOX (statusbar),
                            GTK_WIDGET (statusbar->priv->repeat), FALSE, TRUE, 0);

        gtk_box_pack_start (GTK_BOX (statusbar),
                            GTK_WIDGET (statusbar->priv->status), TRUE, TRUE, 0);

        gtk_box_pack_end (GTK_BOX (statusbar),
                          GTK_WIDGET (statusbar->priv->progress), FALSE, TRUE, 0);

	statusbar->priv->notify_id = 
		eel_gconf_notification_add (CONF_UI_STATUSBAR_HIDDEN,
					    (GConfClientNotifyFunc) rb_statusbar_state_changed_cb,
					    statusbar);
}

static void
rb_statusbar_finalize (GObject *object)
{
        RBStatusbar *statusbar;

        g_return_if_fail (object != NULL);
        g_return_if_fail (RB_IS_STATUSBAR (object));

        statusbar = RB_STATUSBAR (object);

        g_return_if_fail (statusbar->priv != NULL);

	eel_gconf_notification_remove (statusbar->priv->notify_id);

        if (statusbar->priv->idle_tick_id) {
                g_source_remove (statusbar->priv->idle_tick_id);
                statusbar->priv->idle_tick_id = 0;
        }
        if (statusbar->priv->status_poll_id)
                g_source_remove (statusbar->priv->status_poll_id);
        
        g_free (statusbar->priv);

        G_OBJECT_CLASS (parent_class)->finalize (object);
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
                        RBEntryView *songs = rb_source_get_entry_view (statusbar->priv->selected_source);

                        g_signal_handlers_disconnect_by_func (G_OBJECT (songs),
                                                              G_CALLBACK (rb_statusbar_entry_view_changed_cb),
                                                              statusbar);
                }
                
                statusbar->priv->selected_source = g_value_get_object (value);
                rb_debug ("selected source %p", g_value_get_object (value));

                if (statusbar->priv->selected_source != NULL) {
                        RBEntryView *songs = rb_source_get_entry_view (statusbar->priv->selected_source);

                        g_signal_connect_object (G_OBJECT (songs),
                                                 "changed",
                                                 G_CALLBACK (rb_statusbar_entry_view_changed_cb),
                                                 statusbar, 0);

                }
                rb_statusbar_sync_with_source (statusbar);

                break;
        case PROP_ACTION_GROUP:
                statusbar->priv->actiongroup = g_value_get_object (value);
		gtk_action_group_add_toggle_actions (statusbar->priv->actiongroup,
						     rb_statusbar_toggle_entries,
						     rb_statusbar_n_toggle_entries,
						     statusbar);
                break;
        case PROP_PLAYER:
                statusbar->priv->player = g_value_get_object (value);
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
        case PROP_PLAYER:
                g_value_set_object (value, statusbar->priv->player);
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
        char *str;
        gboolean library_busy_changed;
        gboolean buffering_changed;
        gboolean library_is_busy;
        gboolean buffering;
        gboolean entry_view_busy = FALSE;
        gboolean changed = FALSE;

        str = rhythmdb_get_status (status->priv->db);

        if (!str && status->priv->selected_source) {
                RBEntryView *view;
                view = rb_source_get_entry_view (status->priv->selected_source);
                entry_view_busy = rb_entry_view_busy (view);
        }

        library_is_busy = str != NULL;
        library_busy_changed = status->priv->library_busy && !library_is_busy;
        status->priv->library_busy = library_is_busy;   

        g_object_get (G_OBJECT (status->priv->player), "buffering",
                      &buffering, NULL);
        buffering_changed = status->priv->buffering && !buffering;
        status->priv->buffering = buffering;    

        /* Set up the status display */
        if (status->priv->library_busy) {
                gtk_label_set_markup (GTK_LABEL (status->priv->status), str);
                g_free (str);
                str = NULL;
                changed = TRUE;
        } else if (buffering) {
                rb_debug("synchronizing status, showing buffering state");
                g_free (str);
                str = g_strdup_printf ("<b>%s</b>",
                                       _("Buffering..."));
                gtk_label_set_markup (GTK_LABEL (status->priv->status), str);
                g_free (str);
                str = NULL;
        } else if (library_busy_changed || buffering_changed) {
                rb_statusbar_sync_with_source (status);
                changed = TRUE;
        }
        g_free (str);

        /* Sync the progress bar */
        if (library_is_busy || entry_view_busy || buffering) {
                if (status->priv->idle_tick_id == 0) {
                        status->priv->idle_tick_id
                                = g_timeout_add (250, (GSourceFunc) status_tick_cb,
                                                 status->priv->progress);
                        changed = TRUE;
                }
        } else {
                if (status->priv->idle_tick_id > 0) {
                        g_source_remove (status->priv->idle_tick_id);
                        status->priv->idle_tick_id = 0;
                        changed = TRUE;
                }
                gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (status->priv->progress),
                                               1.0);
        }

        status->priv->status_poll_id = 
                g_timeout_add (changed ? 350 : 750, (GSourceFunc) poll_status, status);
}

static gboolean
poll_status (RBStatusbar *status)
{
        GDK_THREADS_ENTER ();

        rb_statusbar_sync_status (status);

        GDK_THREADS_LEAVE ();

        return FALSE;
}

RBStatusbar *
rb_statusbar_new (RhythmDB *db, GtkActionGroup *actions, 
                  RBShellPlayer *player)
{
        RBStatusbar *statusbar = g_object_new (RB_TYPE_STATUSBAR,
                                               "db", db,
                                               "player", player,
                                               "action-group", actions,
                                               NULL);

        g_return_val_if_fail (statusbar->priv != NULL, NULL);

        return statusbar;
}

static void
rb_statusbar_sync_with_source (RBStatusbar *statusbar)
{
        const char *status_str
                = rb_source_get_status (statusbar->priv->selected_source);
        gtk_label_set_markup (GTK_LABEL (statusbar->priv->status), status_str);
}

void
rb_statusbar_sync_state (RBStatusbar *statusbar)
{
	GtkAction *action;
        gboolean hidden, consistent;
        int shuffle, repeat;

        rb_debug ("syncing state");

	statusbar->priv->syncing_state = TRUE;
        consistent = rb_shell_player_get_playback_state (statusbar->priv->player,
                                                         &shuffle, &repeat);
        if (consistent) {
                gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (statusbar->priv->shuffle),
                                              shuffle);
                gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (statusbar->priv->repeat),
                                              repeat);
        } 
        gtk_toggle_button_set_inconsistent (GTK_TOGGLE_BUTTON (statusbar->priv->shuffle),
                                            !consistent);
        gtk_toggle_button_set_inconsistent (GTK_TOGGLE_BUTTON (statusbar->priv->repeat),
                                            !consistent);
        
        hidden = eel_gconf_get_boolean (CONF_UI_STATUSBAR_HIDDEN);
        if (hidden)
                gtk_widget_hide (GTK_WIDGET (statusbar));
        else
                gtk_widget_show (GTK_WIDGET (statusbar));
	action = gtk_action_group_get_action (statusbar->priv->actiongroup,
					      "ViewStatusbar");
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), !hidden);
	statusbar->priv->syncing_state = FALSE;
}
	
static void
rb_statusbar_state_changed_cb (GConfClient *client,
                               guint cnxn_id,
                               GConfEntry *entry,
                               RBStatusbar *statusbar)
{
        rb_debug ("state changed");
	if (statusbar->priv->syncing_state)
		return;
	statusbar->priv->syncing_state = TRUE;
	rb_statusbar_sync_state (statusbar);
}

static void
rb_statusbar_shell_play_order_changed_cb (RBShellPlayer *player, RBStatusbar *statusbar)
{
        rb_debug ("state changed");
	if (statusbar->priv->syncing_state)
		return;
	statusbar->priv->syncing_state = TRUE;
	rb_statusbar_sync_state (statusbar);
}

static void
rb_statusbar_toggle_changed_cb (GtkToggleButton *toggle,
                                RBStatusbar *statusbar)
{
        RBShellPlayer *player = statusbar->priv->player;
        gboolean shuffle, repeat;

	if (statusbar->priv->syncing_state)
		return;
	statusbar->priv->syncing_state = TRUE;

        rb_debug ("toggle changed");

        shuffle = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (statusbar->priv->shuffle));
        repeat = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (statusbar->priv->repeat));

        rb_shell_player_set_playback_state (player, shuffle, repeat);
}

static void
rb_statusbar_view_statusbar_changed_cb (GtkAction *action,
					RBStatusbar *statusbar)
{
        rb_debug ("got view statusbar toggle: active: %d");
	if (statusbar->priv->syncing_state)
		return;
	statusbar->priv->syncing_state = TRUE;
	eel_gconf_set_boolean (CONF_UI_STATUSBAR_HIDDEN,
			       !gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)));
}

static void
rb_statusbar_entry_view_changed_cb (RBEntryView *view,
                                    RBStatusbar *statusbar)
{
        rb_debug ("entry view changed");
        if (!(statusbar->priv->library_busy || statusbar->priv->entry_view_busy))
                rb_statusbar_sync_with_source (statusbar);
}
