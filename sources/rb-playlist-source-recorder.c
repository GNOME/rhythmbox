/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 *  arch-tag: Implementation of playlist source recorder object
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003 Colin Walters <walters@gnome.org>
 *  Copyright (C) 2004 William Jon McCann <mccann@jhu.edu>
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
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <gtk/gtk.h>
#include <glib/gprintf.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomevfs/gnome-vfs-uri.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <glade/glade.h>
#include <bacon-cd-selection.h>
#include "rb-file-helpers.h"
#include "rb-glade-helpers.h"
#include "rb-preferences.h"
#include "rb-dialog.h"
#include "rb-util.h"
#include "rb-playlist-source.h"
#include "rb-playlist-source-recorder.h"
#include "rb-debug.h"
#include "eel-gconf-extensions.h"
#include "gst-hig-dialog.h"

#ifndef HAVE_MKDTEMP
#include "mkdtemp.h"
#else
extern char *mkdtemp (char *template);
#endif

#include "rb-recorder.h"

#define AUDIO_BYTERATE (2 * 44100 * 2)

static void rb_playlist_source_recorder_class_init (RBPlaylistSourceRecorderClass *klass);
static void rb_playlist_source_recorder_init       (RBPlaylistSourceRecorder *source);
static void rb_playlist_source_recorder_finalize   (GObject *object);

void        rb_playlist_source_recorder_device_changed_cb  (BaconCdSelection *bcs,
                                                            const char       *device_path,
                                                            gpointer          data);

GtkWidget * rb_playlist_source_recorder_device_menu_create (void);

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

        char        *name;

        RBRecorder  *recorder;
        GSList      *songs;
        GSList      *current;
        GConfClient *gconf_client;

        GtkWidget   *vbox;
        GtkWidget   *multiple_copies_checkbutton;
        GtkWidget   *cancel_button;
        GtkWidget   *burn_button;
        GtkWidget   *message_label;
        GtkWidget   *track_progress;
        GtkWidget   *album_progress;
        GtkWidget   *device_menu;
        GtkWidget   *options_box;
        GtkWidget   *track_progress_frame;
        GtkWidget   *album_progress_frame;

        gboolean     burning;
        gboolean     already_converted;
        gboolean     handling_error;
        gboolean     confirmed_exit;

        char        *tmp_dir;
};

static GObjectClass *parent_class = NULL;

typedef enum {
        NAME_CHANGED,
        FILE_ADDED,
        ERROR,
        LAST_SIGNAL
} RBPlaylistSourceRecorderSignalType;

static guint rb_playlist_source_recorder_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE(RBPlaylistSourceRecorder, rb_playlist_source_recorder, GTK_TYPE_DIALOG)

static void
rb_playlist_source_recorder_style_set (GtkWidget *widget,
                                       GtkStyle  *previous_style)
{
        GtkDialog *dialog;

        if (GTK_WIDGET_CLASS (parent_class)->style_set)
                GTK_WIDGET_CLASS (parent_class)->style_set (widget, previous_style);

        dialog = GTK_DIALOG (widget);

        gtk_container_set_border_width (GTK_CONTAINER (dialog->vbox), 12);
        gtk_box_set_spacing (GTK_BOX (dialog->vbox), 24);

        gtk_container_set_border_width (GTK_CONTAINER (dialog->action_area), 0);
        gtk_box_set_spacing (GTK_BOX (dialog->action_area), 6);
}


static void
rb_playlist_source_recorder_class_init (RBPlaylistSourceRecorderClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);
        GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

        parent_class = g_type_class_peek_parent (klass);

        widget_class->style_set    = rb_playlist_source_recorder_style_set;

        object_class->finalize = rb_playlist_source_recorder_finalize;

        g_type_class_add_private (klass, sizeof (RBPlaylistSourceRecorderPrivate));

        rb_playlist_source_recorder_signals[NAME_CHANGED] =
                g_signal_new ("name_changed",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              0,
                              NULL, NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);
        
        rb_playlist_source_recorder_signals[FILE_ADDED] =
                g_signal_new ("file_added",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              0,
                              NULL, NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_STRING);

}

GtkWidget *
rb_playlist_source_recorder_device_menu_create (void)
{
        GtkWidget *widget;
        char      *value;

        widget = bacon_cd_selection_new ();
        g_object_set (widget, "file-image", FALSE, NULL);
        /*g_object_set (widget, "show-recorders-only", TRUE, NULL);*/

	value = eel_gconf_get_string (CONF_STATE_BURN_DEVICE);
        if (value) {
                bacon_cd_selection_set_device (BACON_CD_SELECTION (widget),
                                               value);
                g_free (value);
        }

        gtk_widget_show (widget);

        return widget;
}

void
rb_playlist_source_recorder_device_changed_cb (BaconCdSelection *bcs,
                                               const char       *device_path,
                                               gpointer          data)
{
        if (!device_path)
                return;

        eel_gconf_set_string (CONF_STATE_BURN_DEVICE, device_path);
}

static void
set_media_device (RBPlaylistSourceRecorder *source)
{
        const char *device;
        GError     *error;

        device = bacon_cd_selection_get_device (BACON_CD_SELECTION (source->priv->device_menu));
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

        gtk_dialog_response (GTK_DIALOG (source),
                             GTK_RESPONSE_CANCEL);

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

        dialog = gst_hig_dialog_new (GTK_WINDOW (source),
                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                     GST_HIG_MESSAGE_ERROR,
                                     primary, text,
                                     _("Close"), GTK_RESPONSE_CLOSE,
                                     NULL);

        gtk_window_set_title (GTK_WINDOW (dialog),
                              _("Error creating audio CD"));

        g_signal_connect (dialog,
                          "response",
                          G_CALLBACK (error_dialog_response_cb),
                          source);

        gtk_widget_show (dialog);

        g_free (text);
}

static gboolean
burn_cd (RBPlaylistSourceRecorder *source,
         GError                  **error)
{
        gboolean res;

        set_media_device (source);

        set_message_text (source, _("Burning audio to CD"));

        gtk_widget_hide (GTK_WIDGET (source->priv->track_progress_frame));
        gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (source->priv->album_progress), 0);

        source->priv->burning = TRUE;
        res = rb_recorder_burn (source->priv->recorder, error);
        source->priv->burning = FALSE;

        if (error && *error)
                return FALSE;

        if (res) {
                gboolean do_another = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (source->priv->multiple_copies_checkbutton));
                if (!do_another) {
                        set_message_text (source, _("Finished creating audio CD."));
                        gtk_widget_set_sensitive (GTK_WIDGET (source), FALSE);
                        g_idle_add ((GSourceFunc)response_idle_cb, source);
                        return res;
                }
                set_message_text (source, _("Finished creating audio CD.\nCreate another copy?"));
        } else
                set_message_text (source, _("Writing cancelled.  Try again?"));  

        gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (source->priv->album_progress), 0);

        gtk_widget_set_sensitive (GTK_WIDGET (source->priv->burn_button), TRUE);
        gtk_widget_set_sensitive (GTK_WIDGET (source->priv->options_box), TRUE);

        return res;
}

static void
write_file (RBPlaylistSourceRecorder *source,
            GError                  **error)
{
        RBRecorderSong *song   = source->priv->current->data;
        char           *cdtext = NULL;

        gtk_widget_set_sensitive (source->priv->track_progress_frame, TRUE);
        gtk_widget_set_sensitive (source->priv->album_progress_frame, TRUE);

        if (song->artist && song->title)
                cdtext = g_strdup_printf ("%s - %s", song->artist, song->title);
        else if (song->title)
                cdtext = g_strdup (song->title);
        else if (song->artist)
                cdtext = g_strdup (song->title);

        rb_recorder_open (source->priv->recorder, song->uri, cdtext, error);
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

        burn_cd (source, &error);
        if (error) {
                error_dialog (source,
                              _("Audio recording error"),
                              error->message);
                g_error_free (error);
        }

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
        gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (source->priv->track_progress), 0);

        if (source->priv->current->next) {
                int num, total;

                source->priv->current = source->priv->current->next;

                num = g_slist_position (source->priv->songs, source->priv->current);
                total = g_slist_length (source->priv->songs);
                if (num > 0) {
                        float percent = CLAMP ((float)num / (float)total, 0, 1);
                        
                        gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (source->priv->album_progress),
                                                       percent);
                }
                write_file (source, &error);
                if (error) {
                        error_dialog (source,
                                      _("Audio Conversion Error"),
                                      error->message);
                        g_error_free (error);
                        return;
                }
        } else {
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
                           gpointer data)
{
        RBPlaylistSourceRecorder *source = RB_PLAYLIST_SOURCE_RECORDER (data);
        float album_fraction;
        int   num;
        int   total;

        num = g_slist_position (source->priv->songs, source->priv->current);
        total = g_slist_length (source->priv->songs);

        album_fraction = CLAMP ((((float)num + fraction) / (float)total), 0, 1);

        gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (source->priv->track_progress),
                                       fraction);
        gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (source->priv->album_progress),
                                       album_fraction);
}
static void
interrupt_burn_dialog_response_cb (GtkDialog *dialog,
                                   gint       response_id,
                                   gpointer   data)
{
        RBPlaylistSourceRecorder *source = RB_PLAYLIST_SOURCE_RECORDER (data);

        if (response_id == GTK_RESPONSE_ACCEPT) {
                source->priv->confirmed_exit = TRUE;
                gtk_dialog_response (GTK_DIALOG (source),
                                     GTK_RESPONSE_CANCEL);
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
              || response_id == GTK_RESPONSE_CANCEL)) {
                g_signal_stop_emission_by_name (dialog, "response");
                return;
        }

        if (response_id == GTK_RESPONSE_CANCEL) {
                if (source->priv->burning
                    && !source->priv->confirmed_exit) {
                        GtkWidget *interrupt_dialog;

                        source->priv->confirmed_exit = FALSE;

                        interrupt_dialog = gst_hig_dialog_new (GTK_WINDOW (dialog),
                                                               GTK_DIALOG_DESTROY_WITH_PARENT,
                                                               GST_HIG_MESSAGE_QUESTION,
                                                               _("Do you wish to interrupt writing this disc?"),
                                                               _("This may result in an unusable disc."),
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

        if (busy_cd) {
                msg = N_("Please make sure another application is not using the disc.");
                title = N_("Disc is busy");
        } else if (is_reload && can_rewrite) {
                msg = N_("Please insert a rewritable or blank media in the drive tray.");
                title = N_("Insert rewritable or blank media");
        } else if (is_reload && !can_rewrite) {
                msg = N_("Please insert a blank media in the drive tray.");
                title = N_("Insert blank media");
        } else if (can_rewrite) {
                msg = N_("Please replace the in-drive media by a rewritable or blank media.");
                title = N_("Reload rewritable or blank media");
        } else {
                msg = N_("Please replace the in-drive media by a blank media.");
                title = N_("Reload blank media");
        }

        dialog = gst_hig_dialog_new (GTK_WINDOW (source),
                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                     GST_HIG_MESSAGE_ERROR,
                                     title,
                                     msg,
                                     _("Close"), GTK_RESPONSE_CLOSE,
                                     NULL);

        gtk_window_set_title (GTK_WINDOW (dialog), title);

        g_signal_connect (dialog, "response",
                          G_CALLBACK (gtk_widget_destroy), NULL);

        gtk_widget_show (dialog);

        return FALSE;
}

static void
burn_progress_changed_cb (RBRecorder *recorder,
                          gdouble     fraction,
                          gpointer    data)
{
        RBPlaylistSourceRecorder *source = RB_PLAYLIST_SOURCE_RECORDER (data);

        gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (source->priv->album_progress),
                                       fraction);

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
                text = N_("Converting audio track");
                break;
        case RB_RECORDER_ACTION_DISC_PREPARING_WRITE:
                text = N_("Preparing to write disc");
                break;
        case RB_RECORDER_ACTION_DISC_WRITING:
                text = N_("Writing disc");
                break;
        case RB_RECORDER_ACTION_DISC_FIXATING:
                text = N_("Fixating disc");
                break;
        case RB_RECORDER_ACTION_DISC_BLANKING:
                text = N_("Erasing disc");
                break;
        default:
                g_warning (_("Unhandled action in burn_action_changed_cb"));
        }

        if (text)
                set_message_text (source, text);
}

static void
rb_playlist_source_recorder_init (RBPlaylistSourceRecorder *source)
{
        GladeXML       *xml;
        GError         *error = NULL;
        GtkWidget      *widget;
        GtkWidget      *hbox;
        int             font_size;
	PangoAttrList  *pattrlist;
	PangoAttribute *attr;

        source->priv = g_new0 (RBPlaylistSourceRecorderPrivate, 1);
        source->priv->gconf_client = gconf_client_get_default ();

        gtk_dialog_set_has_separator (GTK_DIALOG (source), FALSE);
        source->priv->cancel_button =  gtk_dialog_add_button (GTK_DIALOG (source),
                                                              GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

        source->priv->burn_button = gtk_button_new ();
        GTK_WIDGET_SET_FLAGS (source->priv->burn_button, GTK_CAN_DEFAULT);

        widget = gtk_alignment_new (0.5, 0.5, 0, 0);
        gtk_container_add (GTK_CONTAINER (source->priv->burn_button), widget);
        gtk_widget_show (widget);
        hbox = gtk_hbox_new (FALSE, 6);
        gtk_container_add (GTK_CONTAINER (widget), hbox);
        gtk_widget_show (hbox);
        widget = gtk_image_new_from_stock (GTK_STOCK_CDROM, GTK_ICON_SIZE_BUTTON);
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


        xml = rb_glade_xml_new ("recorder.glade",
                                "recorder_vbox",
                                source);

        source->priv->vbox = glade_xml_get_widget (xml, "recorder_vbox");

        source->priv->message_label  = glade_xml_get_widget (xml, "message_label");
        source->priv->track_progress = glade_xml_get_widget (xml, "track_progress");
        source->priv->album_progress = glade_xml_get_widget (xml, "album_progress");
        source->priv->track_progress_frame = glade_xml_get_widget (xml, "track_progress_frame");
        source->priv->album_progress_frame = glade_xml_get_widget (xml, "album_progress_frame");

        source->priv->options_box    = glade_xml_get_widget (xml, "options_box");
        source->priv->device_menu    = glade_xml_get_widget (xml, "device_menu");
        source->priv->multiple_copies_checkbutton = glade_xml_get_widget (xml, "multiple_copies_checkbutton");

        pattrlist = pango_attr_list_new ();
        attr = pango_attr_weight_new (PANGO_WEIGHT_BOLD);
        attr->start_index = 0;
        attr->end_index = G_MAXINT;
        pango_attr_list_insert (pattrlist, attr);

        widget = glade_xml_get_widget (xml, "device_frame_label");
        gtk_label_set_attributes (GTK_LABEL (widget), pattrlist);

        widget = glade_xml_get_widget (xml, "track_progress_frame_label");
        gtk_label_set_attributes (GTK_LABEL (widget), pattrlist);

        widget = glade_xml_get_widget (xml, "album_progress_frame_label");
        gtk_label_set_attributes (GTK_LABEL (widget), pattrlist);

        widget = glade_xml_get_widget (xml, "options_expander_label");
        gtk_label_set_attributes (GTK_LABEL (widget), pattrlist);

        pango_attr_list_unref (pattrlist);

        pattrlist = pango_attr_list_new ();
        attr = pango_attr_weight_new (PANGO_WEIGHT_BOLD);
        attr->start_index = 0;
        attr->end_index = G_MAXINT;
        pango_attr_list_insert (pattrlist, attr);

        font_size = pango_font_description_get_size (GTK_WIDGET (source->priv->message_label)->style->font_desc);
        attr = pango_attr_size_new (font_size * 1.2);
        attr->start_index = 0;
        attr->end_index = G_MAXINT;
        pango_attr_list_insert (pattrlist, attr);

        gtk_label_set_attributes (GTK_LABEL (source->priv->message_label), pattrlist);

        pango_attr_list_unref (pattrlist);

        gtk_box_pack_start (GTK_BOX (GTK_DIALOG (source)->vbox),
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
                                                 msg);
                gtk_dialog_run (GTK_DIALOG (dialog));
                g_free (msg);
                return;
        }

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
rb_playlist_source_recorder_finalize (GObject *object)
{
        RBPlaylistSourceRecorder *source;
        GSList *l;

        g_return_if_fail (object != NULL);
        g_return_if_fail (RB_IS_PLAYLIST_SOURCE_RECORDER (object));

        source = RB_PLAYLIST_SOURCE_RECORDER (object);

        g_return_if_fail (source->priv != NULL);

        rb_debug ("Finalize source recorder");

        if (source->priv->gconf_client)
                g_object_unref (source->priv->gconf_client);
        source->priv->gconf_client = NULL;

        for (l = source->priv->songs; l; l = l->next)
                recorder_song_free ((RBRecorderSong *)l->data);

        g_free (source->priv->name);
        source->priv->name = NULL;

        g_slist_free (source->priv->songs);
        source->priv->songs = NULL;

        g_object_unref (source->priv->recorder);
        source->priv->recorder = NULL;

        if (source->priv->tmp_dir) {
                if (rmdir (source->priv->tmp_dir) < 0)
                        g_warning (_("Could not remove temporary directory '%s': %s"),
                                   source->priv->tmp_dir,
                                   g_strerror (errno));
                g_free (source->priv->tmp_dir);
                source->priv->tmp_dir = NULL;
        }

        g_free (source->priv);
        source->priv = NULL;

        G_OBJECT_CLASS (parent_class)->finalize (object);
}

GtkWidget *
rb_playlist_source_recorder_new (GtkWidget  *parent,
                                 const char *name)
{
        GtkWidget *result;

        result = g_object_new (RB_TYPE_PLAYLIST_SOURCE_RECORDER,
                               "title", _("Create Audio CD"),
                               NULL);

        if (parent) {
                RBPlaylistSourceRecorder *source = RB_PLAYLIST_SOURCE_RECORDER (result);

                source->priv->parent = gtk_widget_get_toplevel (parent);

                gtk_window_set_transient_for (GTK_WINDOW (source),
                                              GTK_WINDOW (source->priv->parent));
                gtk_window_set_destroy_with_parent (GTK_WINDOW (source), TRUE);
        }

        if (name) {
                RBPlaylistSourceRecorder *source = RB_PLAYLIST_SOURCE_RECORDER (result);

                source->priv->name = g_strdup (name);

                set_message_text (source, _("Create audio CD from '%s' playlist?"), name);
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
                       rb_playlist_source_recorder_signals[NAME_CHANGED],
                       0,
                       name);
}

void
rb_playlist_source_recorder_add_from_model (RBPlaylistSourceRecorder *source,
                                            GtkTreeModel             *model,
                                            RBPlaylistSourceIterFunc  func,
                                            GError                  **error)
{
        GtkTreeIter iter;

        g_return_if_fail (source != NULL);
        g_return_if_fail (RB_IS_PLAYLIST_SOURCE_RECORDER (source));

        g_return_if_fail (model != NULL);

        if (!gtk_tree_model_get_iter_first (model, &iter))
                return;

        do {
                RBRecorderSong *song = recorder_song_new ();
                
                func (model, &iter, &song->uri, &song->artist, &song->title, &song->duration);

                source->priv->songs = g_slist_append (source->priv->songs, song);
                g_signal_emit (G_OBJECT (source),
                               rb_playlist_source_recorder_signals[FILE_ADDED],
                               0,
                               song->uri);
        } while (gtk_tree_model_iter_next (model, &iter));

        return;
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
check_dir_has_space (const char *path,
                     guint64     bytes_needed)
{
        GnomeVFSResult   result     = GNOME_VFS_OK;
        GnomeVFSURI     *dir_uri    = NULL;
        GnomeVFSFileSize free_bytes = 0;

        if (!g_file_test (path, G_FILE_TEST_IS_DIR))
                return FALSE;

        dir_uri = gnome_vfs_uri_new (path);

        result = gnome_vfs_get_volume_free_space (dir_uri, &free_bytes);

        gnome_vfs_uri_unref (dir_uri);

        if (result != GNOME_VFS_OK) {
                g_warning (_("Cannot get free space at %s"), path);
                return FALSE;
        }

        if (bytes_needed >= free_bytes)
                return FALSE;

        return TRUE;
}

static char *
find_tmp_dir (RBPlaylistSourceRecorder *source,
              guint64                   bytes_needed,
              GError                  **error)
{
        char *path;

        g_return_val_if_fail (source != NULL, NULL);
        g_return_val_if_fail (RB_IS_PLAYLIST_SOURCE_RECORDER (source), NULL);

        /* Use a configurable temporary directory? */
        path = NULL;
        if (path && strcmp (path, "")
            && check_dir_has_space (path, bytes_needed))
                return path;
        else if (g_get_tmp_dir () &&
                   check_dir_has_space (g_get_tmp_dir (), bytes_needed))
                return g_strdup (g_get_tmp_dir ());
        else if (g_get_home_dir () &&
                   check_dir_has_space (g_get_home_dir (), bytes_needed))
                return g_strdup (g_get_home_dir ());
        else
                return NULL;
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
                gint64  duration = rb_playlist_source_recorder_get_total_duration (source);
                char   *message  = NULL;
                gint64  media_duration;

                set_media_device (source);
                
                media_duration = rb_recorder_get_media_length (source->priv->recorder, NULL);

                if ((media_duration < 0) && (duration > 4440)) {
                        message = g_strdup_printf (_("This playlist is %d minutes long.  "
                                                     "This exceeds the length of a standard audio CD.  "
                                                     "If the destination media is larger than a standard audio CD "
                                                     "please insert it in the drive and try again."),
                                                   duration / 60);
                } else if ((media_duration > 0) && (media_duration <= duration)) {
                        message = g_strdup_printf (_("This playlist is %d minutes long.  "
                                                     "This exceeds the %d minute length of the media in the drive."),
                                                   duration / 60,
                                                   media_duration / 60);
                }

                if (message) {
                        error_dialog (source,
                                      _("Playlist too long"),
                                      message);
                        g_free (message);

                        return;
                }

                if (!check_tmp_dir (source, error)) {
                        guint64 bytes_needed = rb_playlist_source_recorder_estimate_total_size (source);
                        error_dialog (source,
                                      _("Could not find temporary space!"),
                                      _("Could not find enough temporary space to convert audio tracks.  Need %d MiB."),
                                      bytes_needed / 1048576);
                        return;
                }

                write_file (source, error);
        }
}
