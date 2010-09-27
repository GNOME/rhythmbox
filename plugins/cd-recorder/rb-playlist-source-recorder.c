/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003 Colin Walters <walters@gnome.org>
 *  Copyright (C) 2004-2006 William Jon McCann <mccann@jhu.edu>
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

#include "config.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#include <glib/gprintf.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gio/gio.h>

#include <nautilus-burn.h>

#include "gseal-gtk-compat.h"
#include "rb-file-helpers.h"
#include "rb-builder-helpers.h"
#include "rb-preferences.h"
#include "rb-dialog.h"
#include "rb-util.h"
#include "rb-shell.h"
#include "rb-playlist-source.h"
#include "rb-playlist-source-recorder.h"
#include "rb-debug.h"
#include "eel-gconf-extensions.h"
#include "rb-plugin.h"

#ifndef HAVE_MKDTEMP
#include "mkdtemp.h"
#else
extern char *mkdtemp (char *template);
#endif

#include "rb-recorder.h"

#define CONF_STATE_BURN_SPEED CONF_PREFIX "/state/burn_speed"

#define AUDIO_BYTERATE (2 * 44100 * 2)
#define MAX_PLAYLIST_DURATION 6000

static void rb_playlist_source_recorder_class_init (RBPlaylistSourceRecorderClass *klass);
static void rb_playlist_source_recorder_init       (RBPlaylistSourceRecorder *source);
static void rb_playlist_source_recorder_constructed (GObject *object);
static void rb_playlist_source_recorder_dispose    (GObject *object);
static void rb_playlist_source_recorder_finalize   (GObject *object);

static void rb_playlist_source_recorder_set_property (GObject *object,
						      guint prop_id,
						      const GValue *value,
						      GParamSpec *pspec);
static void rb_playlist_source_recorder_get_property (GObject *object,
						      guint prop_id,
						      GValue *value,
						      GParamSpec *pspec);

void        rb_playlist_source_recorder_device_changed_cb  (NautilusBurnDriveSelection *selection,
                                                            const char                 *device_path,
                                                            RBPlaylistSourceRecorder   *source);

typedef struct
{
        char  *artist;
        char  *title;
        char  *uri;
        gulong duration;
} RBRecorderSong;

struct RBPlaylistSourceRecorderPrivate
{
        GtkWidget   *parent;

        RBShell     *shell;
	RBPlugin    *plugin;

        char        *name;

        RBRecorder  *recorder;
        GSList      *songs;
        GSList      *current;
        GTimer      *timer;
        guint64      start_pos;

        GdkPixbuf   *cd_icon;
        GtkWidget   *vbox;
        GtkWidget   *multiple_copies_checkbutton;
        GtkWidget   *cancel_button;
        GtkWidget   *burn_button;
        GtkWidget   *message_label;
        GtkWidget   *progress_label;
        GtkWidget   *progress;
        GtkWidget   *device_menu;
        GtkWidget   *speed_combobox;
        GtkWidget   *options_box;
        GtkWidget   *progress_frame;

        gboolean     burning;
        gboolean     already_converted;
        gboolean     handling_error;
        gboolean     confirmed_exit;

        char        *tmp_dir;
};

enum
{
	PROP_0,
	PROP_SHELL,
	PROP_PLUGIN
};

typedef enum {
        NAME_CHANGED,
        FILE_ADDED,
        ERROR,
        LAST_SIGNAL
} RBPlaylistSourceRecorderSignalType;

static guint rb_playlist_source_recorder_signals [LAST_SIGNAL] = { 0 };

#define RB_PLAYLIST_SOURCE_RECORDER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_PLAYLIST_SOURCE_RECORDER, RBPlaylistSourceRecorderPrivate))

G_DEFINE_TYPE(RBPlaylistSourceRecorder, rb_playlist_source_recorder, GTK_TYPE_DIALOG)

static void
rb_playlist_source_recorder_style_set (GtkWidget *widget,
                                       GtkStyle  *previous_style)
{
        GtkDialog *dialog;
        GtkWidget *action_area;
        GtkWidget *content_area;

        if (GTK_WIDGET_CLASS (rb_playlist_source_recorder_parent_class)->style_set)
                GTK_WIDGET_CLASS (rb_playlist_source_recorder_parent_class)->style_set (widget, previous_style);

        dialog = GTK_DIALOG (widget);

        content_area = gtk_dialog_get_content_area (dialog);
        gtk_container_set_border_width (GTK_CONTAINER (content_area), 12);
        gtk_box_set_spacing (GTK_BOX (content_area), 24);

        action_area = gtk_dialog_get_action_area (dialog);
        gtk_container_set_border_width (GTK_CONTAINER (action_area), 0);
        gtk_box_set_spacing (GTK_BOX (action_area), 6);
}

static void
rb_playlist_source_recorder_class_init (RBPlaylistSourceRecorderClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);
        GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

        widget_class->style_set = rb_playlist_source_recorder_style_set;

	object_class->constructed = rb_playlist_source_recorder_constructed;
        object_class->dispose = rb_playlist_source_recorder_dispose;
        object_class->finalize = rb_playlist_source_recorder_finalize;
	object_class->set_property = rb_playlist_source_recorder_set_property;
	object_class->get_property = rb_playlist_source_recorder_get_property;

	g_object_class_install_property (object_class,
					 PROP_SHELL,
					 g_param_spec_object ("shell",
							       "RBShell",
							       "RBShell object",
							      RB_TYPE_SHELL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_PLUGIN,
					 g_param_spec_object ("plugin",
						 	      "RBPlugin",
							      "RBPlugin instance for the plugin that created the recorder",
							      RB_TYPE_PLUGIN,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

        rb_playlist_source_recorder_signals [NAME_CHANGED] =
                g_signal_new ("name_changed",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              0,
                              NULL, NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);

        rb_playlist_source_recorder_signals [FILE_ADDED] =
                g_signal_new ("file_added",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              0,
                              NULL, NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);

        g_type_class_add_private (klass, sizeof (RBPlaylistSourceRecorderPrivate));
}

static const NautilusBurnDrive *
lookup_current_recorder (RBPlaylistSourceRecorder *source)
{
        const NautilusBurnDrive *drive;

        drive = nautilus_burn_drive_selection_get_drive (NAUTILUS_BURN_DRIVE_SELECTION (source->priv->device_menu));

        return drive;
}

static int
get_speed_selection (GtkWidget *combobox)
{
        int           speed;
        GtkTreeModel *model;
        GtkTreeIter   iter;

        speed = 0;

        if (! gtk_combo_box_get_active_iter (GTK_COMBO_BOX (combobox), &iter))
                return speed;

        model = gtk_combo_box_get_model (GTK_COMBO_BOX (combobox));
        gtk_tree_model_get (model, &iter, 1, &speed, -1);

        return speed;
}

static void
update_speed_combobox (RBPlaylistSourceRecorder *source)
{
        GtkWidget               *combobox;
        char                    *name;
        int                      i;
        int                      default_speed;
	int                      default_speed_index;
        const NautilusBurnDrive *drive;
        GtkTreeModel            *model;
        GtkTreeIter              iter;

        /* Find active recorder: */
        drive = lookup_current_recorder (source);

        /* add speed items: */
        combobox = source->priv->speed_combobox;
        model = gtk_combo_box_get_model (GTK_COMBO_BOX (combobox));
        gtk_list_store_clear (GTK_LIST_STORE (model));

        gtk_list_store_append (GTK_LIST_STORE (model), &iter);
        gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                            0, _("Maximum possible"),
                            1, 0,
                            -1);

        default_speed = eel_gconf_get_integer (CONF_STATE_BURN_SPEED);
        default_speed_index = -1;

        i = 0;
        if (drive) {
                const int *write_speeds;

                write_speeds = nautilus_burn_drive_get_write_speeds ((NautilusBurnDrive *)drive);

                for (i = 0; write_speeds [i] > 0; i++) {

                        name = g_strdup_printf ("%d \303\227", (int)NAUTILUS_BURN_DRIVE_CD_SPEED (write_speeds [i]));

                        if (write_speeds [i] == default_speed) {
                                default_speed_index = i + 1;
                        }

                        gtk_list_store_append (GTK_LIST_STORE (model), &iter);
                        gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                                            0, name,
                                            1, write_speeds [i],
                                            -1);
                        g_free (name);
                }

        }

        /* Disable speed if no items in list */
        gtk_widget_set_sensitive (combobox, i > 0);

        /* if the default speed was not set then make it the minimum for safety */
        if (default_speed_index == -1) {
                default_speed_index = i;
        }

        /* for now assume equivalence between index in comboxbox and speed */
        gtk_combo_box_set_active (GTK_COMBO_BOX (combobox), default_speed_index);
}

void
rb_playlist_source_recorder_device_changed_cb (NautilusBurnDriveSelection *selection,
                                               const char                 *device_path,
                                               RBPlaylistSourceRecorder   *source)
{
        if (!device_path)
                return;

        eel_gconf_set_string (CONF_STATE_BURN_DEVICE, device_path);

        update_speed_combobox (source);
}

static void
set_media_device (RBPlaylistSourceRecorder *source)
{
        const char *device;
        GError     *error;

        device = nautilus_burn_drive_selection_get_device (NAUTILUS_BURN_DRIVE_SELECTION (source->priv->device_menu));

        if (device && strcmp (device, "")) {
                rb_recorder_set_device (source->priv->recorder, device, &error);
                if (error) {
                        g_warning (_("Invalid writer device: %s"), device);
                        /* ignore and let rb_recorder try to find a default */
                }
        }
}

static void
set_message_text (RBPlaylistSourceRecorder *source,
                  const char               *message,
                  ...)
{
        char   *markup;
        char   *text = "";
        va_list args;

        va_start (args, message);
        g_vasprintf (&text, message, args);
        va_end (args);

        markup = g_strdup_printf ("%s", text);

        gtk_label_set_text (GTK_LABEL (source->priv->message_label), markup);
        g_free (markup);
        g_free (text);
}

static gboolean
response_idle_cb (RBPlaylistSourceRecorder *source)
{
	GDK_THREADS_ENTER ();
        gtk_dialog_response (GTK_DIALOG (source),
                             GTK_RESPONSE_CANCEL);
	GDK_THREADS_LEAVE ();

        return FALSE;
}

static gboolean
error_dialog_response_cb (GtkWidget *dialog,
                          gint       response_id,
                          gpointer   data)
{
        RBPlaylistSourceRecorder *source = RB_PLAYLIST_SOURCE_RECORDER (data);

        gtk_widget_destroy (GTK_WIDGET (dialog));

        g_idle_add ((GSourceFunc)response_idle_cb, source);

        return TRUE;
}

static void
error_dialog (RBPlaylistSourceRecorder *source,
              const char               *primary,
              const char               *secondary,
              ...)
{
        char      *text = "";
        va_list    args;
        GtkWidget *dialog;

        va_start (args, secondary);
        g_vasprintf (&text, secondary, args);
        va_end (args);

        dialog = gtk_message_dialog_new (GTK_WINDOW (source),
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_MESSAGE_ERROR,
                                         GTK_BUTTONS_CLOSE,
                                         "%s", primary);

        gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                                  "%s", text);

        gtk_window_set_title (GTK_WINDOW (dialog), "");

        gtk_container_set_border_width (GTK_CONTAINER (dialog), 6);

        g_signal_connect (dialog,
                          "response",
                          G_CALLBACK (error_dialog_response_cb),
                          source);

        gtk_widget_show (dialog);

        g_free (text);
}

/* Adapted from totem_time_to_string_text */
static char *
time_to_string_text (long time)
{
	char *secs, *mins, *hours, *string;
	int sec, min, hour;

	sec = time % 60;
	time = time - sec;
	min = (time % (60 * 60)) / 60;
	time = time - (min * 60);
	hour = time / (60 * 60);

	hours = g_strdup_printf (ngettext ("%d hour", "%d hours", hour), hour);

	mins = g_strdup_printf (ngettext ("%d minute",
					  "%d minutes", min), min);

	secs = g_strdup_printf (ngettext ("%d second",
					  "%d seconds", sec), sec);

	if (hour > 0) {
		/* hour:minutes:seconds */
		string = g_strdup_printf (_("%s %s %s"), hours, mins, secs);
	} else if (min > 0) {
		/* minutes:seconds */
		string = g_strdup_printf (_("%s %s"), mins, secs);
	} else if (sec > 0) {
		/* seconds */
		string = g_strdup_printf (_("%s"), secs);
	} else {
		/* 0 seconds */
		string = g_strdup (_("0 seconds"));
	}

	g_free (hours);
	g_free (mins);
	g_free (secs);

	return string;
}

static void
progress_set_time (GtkWidget *progress,
                   long       seconds)
{
        char *text;

	if (seconds >= 0) {
		char *remaining;
		remaining = time_to_string_text (seconds);
		text = g_strdup_printf (_("About %s left"), remaining);
		g_free (remaining);
	} else {
		text = g_strdup (" ");
	}

        gtk_progress_bar_set_text (GTK_PROGRESS_BAR (progress), text);
        g_free (text);
}

static void
progress_set_fraction (GtkWidget *progress,
                       gdouble    fraction)
{
        gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (progress), fraction);
}

static int
burn_cd (RBPlaylistSourceRecorder *source,
         GError                  **error)
{
        int res;
        int speed;

        speed = 1;

        set_media_device (source);

        set_message_text (source, _("Writing audio to CD"));

        speed = get_speed_selection (source->priv->speed_combobox);

        progress_set_fraction (source->priv->progress, 0);
        progress_set_time (source->priv->progress, -1);

        source->priv->burning = TRUE;
        res = rb_recorder_burn (source->priv->recorder, speed, error);
        source->priv->burning = FALSE;

        if (res == RB_RECORDER_RESULT_FINISHED) {
                NautilusBurnDrive *drive;
                gboolean           do_another;
                const char        *finished_msg;

                finished_msg = _("Finished creating audio CD.");

                rb_shell_notify_custom (source->priv->shell, 0, finished_msg, "", source->priv->cd_icon, FALSE);

                /* save the write speed that was used */
                eel_gconf_set_integer (CONF_STATE_BURN_SPEED, speed);

                /* Always eject the disk after writing.  Too many drives mess up otherwise */
                drive = (NautilusBurnDrive *)lookup_current_recorder (source);
                nautilus_burn_drive_eject (drive);

                do_another = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (source->priv->multiple_copies_checkbutton));
                if (!do_another) {
                        set_message_text (source, finished_msg);
                        gtk_widget_set_sensitive (GTK_WIDGET (source), FALSE);
                        g_idle_add ((GSourceFunc)response_idle_cb, source);
                        return res;
                }
                set_message_text (source, _("Finished creating audio CD.\nCreate another copy?"));
        } else if (res == RB_RECORDER_RESULT_ERROR) {
                set_message_text (source, _("Writing failed.  Try again?"));
        } else {
                set_message_text (source, _("Writing canceled.  Try again?"));
        }

        progress_set_fraction (source->priv->progress, 0);
        progress_set_time (source->priv->progress, -1);

        gtk_widget_set_sensitive (GTK_WIDGET (source->priv->burn_button), TRUE);
        gtk_widget_set_sensitive (GTK_WIDGET (source->priv->options_box), TRUE);

        return res;
}

static char *
get_song_description (RBRecorderSong *song)
{
        char *desc = NULL;

        if (song->artist && song->title)
                desc = g_strdup_printf ("%s - %s", song->title, song->artist);
        else if (song->title)
                desc = g_strdup (song->title);
        else if (song->artist)
                desc = g_strdup (song->artist);

        return desc;
}

static void
write_file (RBPlaylistSourceRecorder *source,
            GError                  **error)
{
        RBRecorderSong *song   = source->priv->current->data;
        char           *cdtext = NULL;
        char           *markup;

        gtk_widget_set_sensitive (source->priv->progress_frame, TRUE);

        cdtext = get_song_description (song);

        markup = g_markup_printf_escaped ("<i>Converting '%s'</i>", cdtext);
        gtk_label_set_markup (GTK_LABEL (source->priv->progress_label), markup);
        g_free (markup);

        rb_recorder_open (source->priv->recorder, song->uri, cdtext, error);

        g_free (cdtext);

        if (error && *error) {
                return;
        }

        rb_recorder_write (source->priv->recorder, error);
        if (error && *error) {
                return;
        }
}

static gboolean
burn_cd_idle (RBPlaylistSourceRecorder *source)
{
        GError *error = NULL;
        int     res;

	GDK_THREADS_ENTER ();

        res = burn_cd (source, &error);
        if (error) {
                error_dialog (source,
                              _("Audio recording error"),
                              error->message);
                g_error_free (error);
        }

	GDK_THREADS_LEAVE ();
        return FALSE;
}

static void
eos_cb (RBRecorder *recorder,
        gpointer    data)
{
        RBPlaylistSourceRecorder *source = RB_PLAYLIST_SOURCE_RECORDER (data);
        GError                   *error  = NULL;

        rb_debug ("Caught eos!");

        rb_recorder_close (source->priv->recorder, NULL);

        gtk_label_set_text (GTK_LABEL (source->priv->progress_label), "");

        if (source->priv->current->next) {

                source->priv->current = source->priv->current->next;

                write_file (source, &error);
                if (error) {
                        error_dialog (source,
                                      _("Audio Conversion Error"),
                                      error->message);
                        g_error_free (error);
                        return;
                }
        } else {
                if (source->priv->timer) {
                        g_timer_destroy (source->priv->timer);
                        source->priv->timer = NULL;
                }

                source->priv->already_converted = TRUE;

                g_idle_add ((GSourceFunc)burn_cd_idle, source);
        }
}

static void
rb_playlist_source_recorder_error (RBPlaylistSourceRecorder *source,
                                   GError                   *error)
{

        if (source->priv->handling_error) {
                rb_debug ("Ignoring error: %s", error->message);
                return;
        }

        rb_debug ("Error: %s", error->message);

        error_dialog (source,
                      _("Recording error"),
                      error->message);

        source->priv->handling_error = FALSE;
        rb_debug ("Exiting error hander");
}

static void
error_cb (GObject *object,
          GError  *error,
          gpointer data)
{
        RBPlaylistSourceRecorder *source = RB_PLAYLIST_SOURCE_RECORDER (data);

	if (source->priv->handling_error)
		return;

        source->priv->handling_error = TRUE;

        rb_playlist_source_recorder_error (source, error);
}

static void
track_progress_changed_cb (GObject *object,
                           double   fraction,
                           long     secs,
                           gpointer data)
{
        RBPlaylistSourceRecorder *source = RB_PLAYLIST_SOURCE_RECORDER (data);
        double  album_fraction;
        double  rate;
        double  elapsed;
        double  album_secs;
        GSList *l;
        guint64 total;
        guint64 prev_total;
        guint64 song_length;
        guint64 position;

        total = 0;
        prev_total = 0;
        song_length = 0;
        for (l = source->priv->songs; l; l = l->next) {
                RBRecorderSong *song = l->data;

                if (song == source->priv->current->data) {
                        prev_total = total;
                        song_length = song->duration;
                }
                total += song->duration;
        }

        position = prev_total + song_length * fraction;
        if (! source->priv->timer) {
                source->priv->timer = g_timer_new ();
                source->priv->start_pos = position;
        }

        album_fraction = (float)position / (float)total;

        elapsed = g_timer_elapsed (source->priv->timer, NULL);

        rate = (double)(position - source->priv->start_pos) / elapsed;

        if (rate >= 1)
                album_secs = ceil ((total - position) / rate);
        else
                album_secs = -1;

        progress_set_time (source->priv->progress, album_secs);
        progress_set_fraction (source->priv->progress, album_fraction);
}
static void
interrupt_burn_dialog_response_cb (GtkDialog *dialog,
                                   gint       response_id,
                                   gpointer   data)
{
        RBPlaylistSourceRecorder *source = RB_PLAYLIST_SOURCE_RECORDER (data);

        if (response_id == GTK_RESPONSE_ACCEPT) {
                if (source->priv->burning) {
                        rb_recorder_burn_cancel (source->priv->recorder);
                } else {
                        source->priv->confirmed_exit = TRUE;
                        gtk_dialog_response (GTK_DIALOG (source),
                                             GTK_RESPONSE_CANCEL);
                }
        } else {
                source->priv->confirmed_exit = FALSE;
        }

        gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
response_cb (GtkDialog *dialog,
             gint       response_id)
{
        RBPlaylistSourceRecorder *source = RB_PLAYLIST_SOURCE_RECORDER (dialog);
        GError                   *error  = NULL;

        /* Act only on response IDs we recognize */
        if (!(response_id == GTK_RESPONSE_ACCEPT
              || response_id == GTK_RESPONSE_CANCEL
	      || response_id == GTK_RESPONSE_DELETE_EVENT)) {
                g_signal_stop_emission_by_name (dialog, "response");
                return;
        }

        if (response_id == GTK_RESPONSE_CANCEL || response_id == GTK_RESPONSE_DELETE_EVENT) {
                if (source->priv->burning
                    && !source->priv->confirmed_exit) {
                        GtkWidget *interrupt_dialog;

                        source->priv->confirmed_exit = FALSE;

                        interrupt_dialog = gtk_message_dialog_new (GTK_WINDOW (dialog),
                                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                                   GTK_MESSAGE_QUESTION,
                                                                   GTK_BUTTONS_NONE,
                                                                   _("Do you wish to interrupt writing this disc?"));

                        gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (interrupt_dialog),
                                                                  _("This may result in an unusable disc."));

                        gtk_window_set_title (GTK_WINDOW (interrupt_dialog), "");

                        gtk_container_set_border_width (GTK_CONTAINER (interrupt_dialog), 6);

                        gtk_dialog_add_buttons (GTK_DIALOG (interrupt_dialog),
                                                _("_Cancel"), GTK_RESPONSE_CANCEL,
                                                _("_Interrupt"), GTK_RESPONSE_ACCEPT,
                                                NULL);

                        gtk_dialog_set_default_response (GTK_DIALOG (interrupt_dialog),
                                                         GTK_RESPONSE_CANCEL);

                        g_signal_connect (interrupt_dialog,
                                          "response",
                                          G_CALLBACK (interrupt_burn_dialog_response_cb),
                                          source);

                        gtk_widget_show (interrupt_dialog);

                        g_signal_stop_emission_by_name (dialog, "response");
                }
                return;
        }

        if (response_id == GTK_RESPONSE_ACCEPT) {
                rb_playlist_source_recorder_start (source, &error);
                if (error) {
                        error_dialog (source,
                                      _("Could not create audio CD"),
                                      error->message);
                        g_error_free (error);
                }
                g_signal_stop_emission_by_name (dialog, "response");
        }
}

static gboolean
insert_media_request_cb (RBRecorder *recorder,
                         gboolean    is_reload,
                         gboolean    can_rewrite,
                         gboolean    busy_cd,
                         gpointer    data)
{
        RBPlaylistSourceRecorder *source = RB_PLAYLIST_SOURCE_RECORDER (data);
        GtkWidget  *dialog;
        const char *msg;
        const char *title;
        int         res;

        if (busy_cd) {
                msg = N_("Please make sure another application is not using the drive.");
                title = N_("Drive is busy");
        } else if (is_reload && can_rewrite) {
                msg = N_("Please put a rewritable or blank CD in the drive.");
                title = N_("Insert a rewritable or blank CD");
        } else if (is_reload && !can_rewrite) {
                msg = N_("Please put a blank CD in the drive.");
                title = N_("Insert a blank CD");
        } else if (can_rewrite) {
                msg = N_("Please replace the disc in the drive with a rewritable or blank CD.");
                title = N_("Reload a rewritable or blank CD");
        } else {
                msg = N_("Please replace the disc in the drive with a blank CD.");
                title = N_("Reload a blank CD");
        }

        GDK_THREADS_ENTER ();
        dialog = gtk_message_dialog_new (GTK_WINDOW (source),
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_MESSAGE_ERROR,
                                         GTK_BUTTONS_OK_CANCEL,
                                         "%s", title);

        gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog), "%s", msg);

        gtk_window_set_title (GTK_WINDOW (dialog), "");

        gtk_container_set_border_width (GTK_CONTAINER (dialog), 6);

        res = gtk_dialog_run (GTK_DIALOG (dialog));

        gtk_widget_destroy (dialog);
        GDK_THREADS_LEAVE ();

        if (res == GTK_RESPONSE_CANCEL)
                return FALSE;

        return TRUE;
}

static void
burn_progress_changed_cb (RBRecorder *recorder,
                          gdouble     fraction,
                          long        secs,
                          gpointer    data)
{
        RBPlaylistSourceRecorder *source = RB_PLAYLIST_SOURCE_RECORDER (data);

        progress_set_fraction (source->priv->progress, fraction);
        progress_set_time (source->priv->progress, secs);
}

static void
burn_action_changed_cb (RBRecorder        *recorder,
                        RBRecorderAction   action,
                        gpointer           data)
{
        RBPlaylistSourceRecorder *source = RB_PLAYLIST_SOURCE_RECORDER (data);
        const char *text;

        text = NULL;

        switch (action) {
        case RB_RECORDER_ACTION_FILE_CONVERTING:
                text = N_("Converting audio tracks");
                break;
        case RB_RECORDER_ACTION_DISC_PREPARING_WRITE:
                text = N_("Preparing to write CD");
                break;
        case RB_RECORDER_ACTION_DISC_WRITING:
                text = N_("Writing CD");
                break;
        case RB_RECORDER_ACTION_DISC_FIXATING:
                text = N_("Finishing write");
                break;
        case RB_RECORDER_ACTION_DISC_BLANKING:
                text = N_("Erasing CD");
                break;
        default:
                g_warning (_("Unhandled action in burn_action_changed_cb"));
        }

        if (text)
                set_message_text (source, text);
}

static int
ask_rewrite_disc (RBPlaylistSourceRecorder *source,
                  const char               *device)
{
        GtkWidget            *dialog;
        GtkWidget            *button;
        GtkWidget            *image;
        int                   res;
        NautilusBurnMediaType type;
        char                 *msg;
        NautilusBurnDrive    *drive;

        drive = nautilus_burn_drive_monitor_get_drive_for_device (nautilus_burn_get_drive_monitor (),
                                                                  device);

        type = nautilus_burn_drive_get_media_type (drive);

        msg = g_strdup_printf (_("This %s appears to have information already recorded on it."),
                               nautilus_burn_drive_media_type_get_string (type));

        dialog = gtk_message_dialog_new (GTK_WINDOW (source),
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_MESSAGE_WARNING,
                                         GTK_BUTTONS_NONE,
                                         "%s", _("Erase information on this disc?"));

        gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog), "%s", msg);
        g_free (msg);

        gtk_window_set_title (GTK_WINDOW (dialog), "");

        image = gtk_image_new_from_stock (GTK_STOCK_REFRESH, GTK_ICON_SIZE_BUTTON);
        gtk_widget_show (image);
        button = gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Try Another"), RB_RECORDER_RESPONSE_RETRY);
        g_object_set (button, "image", image, NULL);

        gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_CANCEL, RB_RECORDER_RESPONSE_CANCEL);

        image = gtk_image_new_from_stock (GTK_STOCK_CLEAR, GTK_ICON_SIZE_BUTTON);
        gtk_widget_show (image);
        button = gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Erase Disc"), RB_RECORDER_RESPONSE_ERASE);
        g_object_set (button, "image", image, NULL);

        gtk_dialog_set_default_response (GTK_DIALOG (dialog),
                                         RB_RECORDER_RESPONSE_CANCEL);

        res = gtk_dialog_run (GTK_DIALOG (dialog));

        gtk_widget_destroy (dialog);

        if (res == RB_RECORDER_RESPONSE_RETRY) {
                nautilus_burn_drive_eject (drive);
        }

        nautilus_burn_drive_unref (drive);

        return res;
}

static int
warn_data_loss_cb (RBRecorder               *recorder,
                   RBPlaylistSourceRecorder *source)
{
        char *device;
        int   res;

        device = rb_recorder_get_device (recorder, NULL);
	GDK_THREADS_ENTER();
        res = ask_rewrite_disc (source, device);
	GDK_THREADS_LEAVE();

        g_free (device);

        return res;
}

static void
setup_speed_combobox (GtkWidget *combobox)
{
        GtkCellRenderer *cell;
        GtkListStore    *store;

        store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_INT);

        gtk_combo_box_set_model (GTK_COMBO_BOX (combobox),
                                 GTK_TREE_MODEL (store));
        g_object_unref (store);

        cell = gtk_cell_renderer_text_new ();
        gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combobox), cell, TRUE);
        gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combobox), cell,
                                        "text", 0,
                                        NULL);
}

static int
delete_event_handler (GtkWidget   *widget,
                      GdkEventAny *event,
                      gpointer     user_data)
{
        /* emit response signal */
        gtk_dialog_response (GTK_DIALOG (widget), GTK_RESPONSE_DELETE_EVENT);

        /* Do the destroy by default */
        return TRUE;
}

static void
rb_playlist_source_recorder_init (RBPlaylistSourceRecorder *source)
{
	source->priv = RB_PLAYLIST_SOURCE_RECORDER_GET_PRIVATE (source);
}

static void
rb_playlist_source_recorder_set_property (GObject *object,
					  guint prop_id,
					  const GValue *value,
					  GParamSpec *pspec)
{
	RBPlaylistSourceRecorder *source;
	source = RB_PLAYLIST_SOURCE_RECORDER (object);

	switch (prop_id) {
	case PROP_SHELL:
		source->priv->shell = g_value_get_object (value);
		break;
	case PROP_PLUGIN:
		source->priv->plugin = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_playlist_source_recorder_get_property (GObject *object,
					  guint prop_id,
					  GValue *value,
					  GParamSpec *pspec)
{
	RBPlaylistSourceRecorder *source;
	source = RB_PLAYLIST_SOURCE_RECORDER (object);

	switch (prop_id) {
	case PROP_SHELL:
		g_value_set_object (value, source->priv->shell);
		break;
	case PROP_PLUGIN:
		g_value_set_object (value, source->priv->plugin);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_playlist_source_recorder_constructed (GObject *object)
{
	RBPlaylistSourceRecorder *source;
	char           *builder_file;
        GtkBuilder     *builder;
        GError         *error = NULL;
        GtkWidget      *widget;
        GtkWidget      *hbox;
        int             font_size;
        PangoAttrList  *pattrlist;
        PangoAttribute *attr;
	char           *value;
        GtkStyle       *style;

        RB_CHAIN_GOBJECT_METHOD (rb_playlist_source_recorder_parent_class, constructed, object);
	source = RB_PLAYLIST_SOURCE_RECORDER (object);

        g_signal_connect (GTK_DIALOG (source),
                          "delete_event",
                          G_CALLBACK (delete_event_handler),
                          NULL);

        source->priv = RB_PLAYLIST_SOURCE_RECORDER_GET_PRIVATE (source);

        gtk_window_set_resizable (GTK_WINDOW (source), FALSE);
        source->priv->cancel_button =  gtk_dialog_add_button (GTK_DIALOG (source),
                                                              GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

        source->priv->burn_button = gtk_button_new ();
        gtk_widget_set_can_default (source->priv->burn_button, TRUE);

        widget = gtk_alignment_new (0.5, 0.5, 0, 0);
        gtk_container_add (GTK_CONTAINER (source->priv->burn_button), widget);
        gtk_widget_show (widget);
        hbox = gtk_hbox_new (FALSE, 6);
        gtk_container_add (GTK_CONTAINER (widget), hbox);
        gtk_widget_show (hbox);

	source->priv->cd_icon = gtk_widget_render_icon (GTK_WIDGET (source),
							GTK_STOCK_CDROM,
							GTK_ICON_SIZE_BUTTON,
							NULL);
	widget = gtk_image_new_from_pixbuf (source->priv->cd_icon);
        gtk_box_pack_start (GTK_BOX (hbox), widget, TRUE, TRUE, 0);
        gtk_widget_show (widget);
        widget = gtk_label_new_with_mnemonic (_("C_reate"));
        gtk_box_pack_start (GTK_BOX (hbox), widget, TRUE, TRUE, 0);
        gtk_widget_show (widget);
        gtk_dialog_add_action_widget (GTK_DIALOG (source),
                                      source->priv->burn_button,
                                      GTK_RESPONSE_ACCEPT);
        gtk_widget_show (source->priv->burn_button);

        gtk_dialog_set_default_response (GTK_DIALOG (source), GTK_RESPONSE_ACCEPT);

	builder_file = rb_plugin_find_file (source->priv->plugin, "recorder.ui");
	g_assert (builder_file != NULL);

	builder = rb_builder_load (builder_file, source);
	g_free (builder_file);

        source->priv->vbox = GTK_WIDGET (gtk_builder_get_object (builder, "recorder_vbox"));

        source->priv->message_label  = GTK_WIDGET (gtk_builder_get_object (builder, "message_label"));
        source->priv->progress_label  = GTK_WIDGET (gtk_builder_get_object (builder, "progress_label"));

        source->priv->progress = GTK_WIDGET (gtk_builder_get_object (builder, "progress"));
        gtk_progress_bar_set_ellipsize (GTK_PROGRESS_BAR (source->priv->progress), PANGO_ELLIPSIZE_END);
        gtk_progress_bar_set_text (GTK_PROGRESS_BAR (source->priv->progress), " ");
        gtk_widget_set_size_request (source->priv->progress, 400, -1);

        source->priv->progress_frame = GTK_WIDGET (gtk_builder_get_object (builder, "progress_frame"));

        source->priv->options_box    = GTK_WIDGET (gtk_builder_get_object (builder, "options_box"));
        source->priv->multiple_copies_checkbutton = GTK_WIDGET (gtk_builder_get_object (builder, "multiple_copies_checkbutton"));

        source->priv->speed_combobox = GTK_WIDGET (gtk_builder_get_object (builder, "speed_combobox"));
        setup_speed_combobox (source->priv->speed_combobox);

        source->priv->device_menu    = nautilus_burn_drive_selection_new ();
        g_object_set (source->priv->device_menu, "file-image", FALSE, NULL);
        g_object_set (source->priv->device_menu, "show-recorders-only", TRUE, NULL);

	value = eel_gconf_get_string (CONF_STATE_BURN_DEVICE);
        if (value) {
                nautilus_burn_drive_selection_set_device (NAUTILUS_BURN_DRIVE_SELECTION (source->priv->device_menu),
                                                          value);
                g_free (value);
        }
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "device_menu_container"));
	gtk_box_pack_start (GTK_BOX (widget), source->priv->device_menu, TRUE, TRUE, 0);

        widget = GTK_WIDGET (gtk_builder_get_object (builder, "device_label"));
        gtk_label_set_mnemonic_widget (GTK_LABEL (widget), source->priv->device_menu);

	rb_builder_boldify_label (builder, "progress_frame_label");
	rb_builder_boldify_label (builder, "options_expander_label");

        pattrlist = pango_attr_list_new ();
        attr = pango_attr_weight_new (PANGO_WEIGHT_BOLD);
        attr->start_index = 0;
        attr->end_index = G_MAXINT;
        pango_attr_list_insert (pattrlist, attr);

        style = gtk_widget_get_style (GTK_WIDGET (source->priv->message_label));
        font_size = pango_font_description_get_size (style->font_desc);
        attr = pango_attr_size_new (font_size * 1.2);
        attr->start_index = 0;
        attr->end_index = G_MAXINT;
        pango_attr_list_insert (pattrlist, attr);

        gtk_label_set_attributes (GTK_LABEL (source->priv->message_label), pattrlist);

        pango_attr_list_unref (pattrlist);

        gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (source))),
                            source->priv->vbox,
                            TRUE, TRUE, 0);
        gtk_widget_show_all (source->priv->vbox);

        source->priv->recorder = rb_recorder_new (&error);
        if (error) {
                GtkWidget *dialog;
                char      *msg = g_strdup_printf (_("Failed to create the recorder: %s"),
                                                  error->message);
                g_error_free (error);
                dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL,
                                                 GTK_MESSAGE_ERROR,
                                                 GTK_BUTTONS_CLOSE,
                                                 "%s", msg);
                gtk_dialog_run (GTK_DIALOG (dialog));
                g_free (msg);
        }

        update_speed_combobox (source);
        g_signal_connect (source->priv->device_menu, "device-changed",
                          G_CALLBACK (rb_playlist_source_recorder_device_changed_cb),
                          source);

        g_signal_connect_object (G_OBJECT (source->priv->recorder), "eos",
                                 G_CALLBACK (eos_cb), source, 0);

        g_signal_connect_object (G_OBJECT (source->priv->recorder), "error",
                                 G_CALLBACK (error_cb), source, 0);

        g_signal_connect_object (G_OBJECT (source->priv->recorder), "action-changed",
                                 G_CALLBACK (burn_action_changed_cb), source, 0);

        g_signal_connect_object (G_OBJECT (source->priv->recorder), "track-progress-changed",
                                 G_CALLBACK (track_progress_changed_cb), source, 0);

        g_signal_connect_object (G_OBJECT (source->priv->recorder), "insert-media-request",
                                 G_CALLBACK (insert_media_request_cb), source, 0);

        g_signal_connect_object (G_OBJECT (source->priv->recorder), "warn-data-loss",
                                 G_CALLBACK (warn_data_loss_cb), source, 0);

        g_signal_connect_object (G_OBJECT (source->priv->recorder), "burn-progress-changed",
                                 G_CALLBACK (burn_progress_changed_cb), source, 0);

        g_signal_connect (GTK_DIALOG (source), "response",
                          G_CALLBACK (response_cb), NULL);
}

static RBRecorderSong *
recorder_song_new ()
{
        RBRecorderSong *song = g_new0 (RBRecorderSong, 1);
        return song;
}

static void
recorder_song_free (RBRecorderSong *song)
{
        g_return_if_fail (song != NULL);

        g_free (song->title);
        g_free (song->uri);
        g_free (song);
}

static void
free_song_list (GSList *songs)
{
        GSList *l;

        for (l = songs; l; l = l->next) {
                recorder_song_free ((RBRecorderSong *)l->data);
        }

        g_slist_free (songs);
        songs = NULL;
}

static void
rb_playlist_source_recorder_dispose (GObject *object)
{
        RBPlaylistSourceRecorder *source;

        g_return_if_fail (object != NULL);
        g_return_if_fail (RB_IS_PLAYLIST_SOURCE_RECORDER (object));

        source = RB_PLAYLIST_SOURCE_RECORDER (object);

        g_return_if_fail (source->priv != NULL);

	if (source->priv->shell != NULL) {
		g_object_unref (source->priv->shell);
		source->priv->shell = NULL;
	}

	if (source->priv->cd_icon != NULL) {
		g_object_unref (source->priv->cd_icon);
		source->priv->cd_icon = NULL;
	}

	if (source->priv->recorder != NULL) {
		g_object_unref (source->priv->recorder);
		source->priv->recorder = NULL;
	}

        G_OBJECT_CLASS (rb_playlist_source_recorder_parent_class)->dispose (object);
}

static void
rb_playlist_source_recorder_finalize (GObject *object)
{
        RBPlaylistSourceRecorder *source;

        g_return_if_fail (object != NULL);
        g_return_if_fail (RB_IS_PLAYLIST_SOURCE_RECORDER (object));

        source = RB_PLAYLIST_SOURCE_RECORDER (object);

        g_return_if_fail (source->priv != NULL);

        rb_debug ("Finalize source recorder");

        g_free (source->priv->name);
        source->priv->name = NULL;

        free_song_list (source->priv->songs);

        if (source->priv->tmp_dir) {
                if (rmdir (source->priv->tmp_dir) < 0)
                        g_warning (_("Could not remove temporary directory '%s': %s"),
                                   source->priv->tmp_dir,
                                   g_strerror (errno));
                g_free (source->priv->tmp_dir);
                source->priv->tmp_dir = NULL;
        }

        G_OBJECT_CLASS (rb_playlist_source_recorder_parent_class)->finalize (object);
}

GtkWidget *
rb_playlist_source_recorder_new (GtkWidget  *parent,
                                 RBShell    *shell,
				 RBPlugin   *plugin,
                                 const char *name)
{
        GtkWidget *result;
        RBPlaylistSourceRecorder *source;

        result = g_object_new (RB_TYPE_PLAYLIST_SOURCE_RECORDER,
                               "title", _("Create Audio CD"),
			       "plugin", plugin,
                               NULL);

        source = RB_PLAYLIST_SOURCE_RECORDER (result);
        if (parent) {
                source->priv->parent = gtk_widget_get_toplevel (parent);

                gtk_window_set_transient_for (GTK_WINDOW (source),
                                              GTK_WINDOW (source->priv->parent));
                gtk_window_set_destroy_with_parent (GTK_WINDOW (source), TRUE);
        }

        source->priv->shell = g_object_ref (shell);

        if (name) {
                source->priv->name = g_strdup (name);

                set_message_text (source, _("Create audio CD from '%s'?"), name);
        }

        return result;
}

void
rb_playlist_source_recorder_set_name (RBPlaylistSourceRecorder *source,
                                      const char               *name,
                                      GError                  **error)
{
        g_return_if_fail (source != NULL);
        g_return_if_fail (RB_IS_PLAYLIST_SOURCE_RECORDER (source));

        g_return_if_fail (name != NULL);

        g_free (source->priv->name);
        source->priv->name = g_strdup (name);

        g_signal_emit (G_OBJECT (source),
                       rb_playlist_source_recorder_signals [NAME_CHANGED],
                       0,
                       name);
}

gboolean
rb_playlist_source_recorder_add_from_model (RBPlaylistSourceRecorder *source,
                                            GtkTreeModel             *model,
                                            RBPlaylistSourceIterFunc  func,
                                            GError                  **error)
{
        GtkTreeIter iter;
        gboolean    failed;
        GSList     *songs  = NULL;
        GSList     *l;
        guint64     length = 0;

        g_return_val_if_fail (source != NULL, FALSE);
        g_return_val_if_fail (RB_IS_PLAYLIST_SOURCE_RECORDER (source), FALSE);

        g_return_val_if_fail (model != NULL, FALSE);

        if (! gtk_tree_model_get_iter_first (model, &iter)) {
                g_set_error (error,
                             RB_RECORDER_ERROR,
                             RB_RECORDER_ERROR_GENERAL,
                             _("Unable to build an audio track list."));

                return FALSE;
        }

        /* Make sure we can use all of the songs before we
           modify the song list */
        failed = FALSE;
        do {
                RBRecorderSong *song = recorder_song_new ();
                gboolean        res;

                res = func (model, &iter, &song->uri, &song->artist, &song->title, &song->duration);
                if (! res) {
                        failed = TRUE;
                        g_set_error (error,
                                     RB_RECORDER_ERROR,
                                     RB_RECORDER_ERROR_GENERAL,
                                     _("Unable to build an audio track list."));
                        break;
                }

                length += song->duration;
                if (length > MAX_PLAYLIST_DURATION) {
                        failed = TRUE;
                        g_set_error (error,
                                     RB_RECORDER_ERROR,
                                     RB_RECORDER_ERROR_GENERAL,
                                     _("This playlist is too long to write to an audio CD."));
                        break;
                }

                songs = g_slist_append (songs, song);
        } while (gtk_tree_model_iter_next (model, &iter));

        if (failed) {
                free_song_list (songs);

                return FALSE;
        }

        /* now that we've checked all the songs, add them to the song list */
        for (l = songs; l; l = l->next) {
                RBRecorderSong *song = l->data;

                source->priv->songs = g_slist_append (source->priv->songs, song);

                g_signal_emit (G_OBJECT (source),
                               rb_playlist_source_recorder_signals [FILE_ADDED],
                               0,
                               song->uri);
        }

        return TRUE;
}

static guint64
rb_playlist_source_recorder_get_total_duration (RBPlaylistSourceRecorder *source)
{
        GSList *l;
        guint64 length = 0;

        for (l = source->priv->songs; l; l = l->next) {
                RBRecorderSong *song = l->data;
                length += song->duration;
        }

        return length;
}

static guint64
rb_playlist_source_recorder_estimate_total_size (RBPlaylistSourceRecorder *source)
{
        guint64 length;

        length = rb_playlist_source_recorder_get_total_duration (source);

        return length * AUDIO_BYTERATE;
}

static gboolean
try_tmp_dir (const char *dir, guint64 bytes_needed)
{
	GFile *file;
	gboolean r;

	if (dir == NULL) {
		return FALSE;
	}

	file = g_file_new_for_path (dir);
	r = rb_check_dir_has_space (file, bytes_needed);
	g_object_unref (file);
	return r;
}

static char *
find_tmp_dir (RBPlaylistSourceRecorder *source,
              guint64                   bytes_needed,
              GError                  **error)
{
        g_return_val_if_fail (source != NULL, NULL);
        g_return_val_if_fail (RB_IS_PLAYLIST_SOURCE_RECORDER (source), NULL);

        /* Use a configurable temporary directory? */
	if (try_tmp_dir (g_get_tmp_dir (), bytes_needed)) {
		return g_strdup (g_get_tmp_dir ());
	} else if (try_tmp_dir (g_get_home_dir (), bytes_needed)) {
		return g_strdup (g_get_home_dir ());
	} else {
		return NULL;
	}
}

static gboolean
check_tmp_dir (RBPlaylistSourceRecorder *source,
               GError                  **error)
{
        char   *path;
        char   *template;
        char   *subdir;
        guint64 bytes_needed;

        g_return_val_if_fail (source != NULL, FALSE);
        g_return_val_if_fail (RB_IS_PLAYLIST_SOURCE_RECORDER (source), FALSE);

        bytes_needed = rb_playlist_source_recorder_estimate_total_size (source);

        path = find_tmp_dir (source, bytes_needed, error);
        if (!path)
                return FALSE;

        template = g_build_filename (path, "rb-burn-tmp-XXXXXX", NULL);
        subdir = mkdtemp (template);

        if (!subdir)
                return FALSE;

        g_free (source->priv->tmp_dir);
        source->priv->tmp_dir = subdir;
        rb_recorder_set_tmp_dir (source->priv->recorder,
                                 source->priv->tmp_dir,
                                 error);

        if (error && *error)
                return FALSE;

        return TRUE;
}

static gboolean
check_media_length (RBPlaylistSourceRecorder *source,
                    GError                  **error)
{
        gint64  duration = rb_playlist_source_recorder_get_total_duration (source);
        char   *message  = NULL;
        gint64  media_duration;
        char   *duration_string;

        media_duration = rb_recorder_get_media_length (source->priv->recorder, NULL);
        duration_string = g_strdup_printf ("%" G_GINT64_FORMAT, duration / 60);

        /* Only check if the playlist is greater than 74 minutes */
        if ((media_duration < 0) && (duration > 4440)) {
                message = g_strdup_printf (_("This playlist is %s minutes long.  "
                                             "This exceeds the length of a standard audio CD.  "
                                             "If the destination medium is larger than a standard audio CD "
                                             "please insert it in the drive and try again."),
                                           duration_string);
        }

        g_free (duration_string);

        if (message) {
                error_dialog (source,
                              _("Playlist too long"),
                              message);
                g_free (message);

                return FALSE;
        }

        return TRUE;
}

void
rb_playlist_source_recorder_start (RBPlaylistSourceRecorder *source,
                                   GError                  **error)
{
        g_return_if_fail (source != NULL);
        g_return_if_fail (RB_IS_PLAYLIST_SOURCE_RECORDER (source));

        source->priv->current = source->priv->songs;

        gtk_widget_set_sensitive (source->priv->burn_button, FALSE);
        gtk_widget_set_sensitive (source->priv->options_box, FALSE);

        if (source->priv->already_converted) {
                g_idle_add ((GSourceFunc)burn_cd_idle, source);
        } else {
                gboolean is_ok;

                set_media_device (source);

                is_ok = check_media_length (source, error);
                if (! is_ok) {
                        return;
                }

                is_ok = check_tmp_dir (source, error);
                if (! is_ok) {
                        guint64 mib_needed = rb_playlist_source_recorder_estimate_total_size (source) / 1048576;
                        char   *mib_needed_string = g_strdup_printf ("%" G_GUINT64_FORMAT, mib_needed);

                        error_dialog (source,
                                      _("Could not find temporary space!"),
                                      _("Could not find enough temporary space to convert audio tracks.  %s MB required."),
                                      mib_needed_string);
                        g_free (mib_needed_string);

                        return;
                }

                write_file (source, error);
        }
}
