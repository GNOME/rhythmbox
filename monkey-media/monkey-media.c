/*  monkey-sound
 *
 *  arch-tag: Implementation of main MonkeyMedia interface
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *                     Marco Pesenti Gritti <marco@it.gnome.org>
 *                     Bastien Nocera <hadess@hadess.net>
 *                     Seth Nickell <snickell@stanford.edu>
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

#include <popt.h>
#include <glib.h>
#include <gconf/gconf.h>
#include <gconf/gconf-client.h>
#include <libgnomevfs/gnome-vfs-init.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnomevfs/gnome-vfs-mime-utils.h>
#include <libgnomevfs/gnome-vfs-mime-info.h>
#include <libgnomevfs/gnome-vfs-mime-handlers.h>
#include <libgnomevfs/gnome-vfs-application-registry.h>
#include <string.h>
#include <sys/stat.h>

#include "config.h"

#ifdef HAVE_GSTREAMER
#include <gst/gst.h>
#include <gst/control/control.h>
#endif

#include "monkey-media.h"
#include "monkey-media-stream-info.h"
#include "monkey-media-private.h"
#include "monkey-media-audio-cd-private.h"
#include "monkey-media-musicbrainz.h"

#include "vorbis-stream-info-impl.h"
#include "mp3-stream-info-impl.h"
#include "audiocd-stream-info-impl.h"
#ifdef HAVE_FLAC
#include "flac-stream-info-impl.h"
#endif
#ifdef HAVE_MP4
#include "mp4-stream-info-impl.h"
#endif

static void monkey_media_init_internal (void);
static void monkey_media_audio_driver_changed (GConfClient *client, guint cnxn_id,
			                       GConfEntry *entry, gpointer user_data);
static void monkey_media_cd_drive_changed (GConfClient *client, guint cnxn_id,
			                   GConfEntry *entry, gpointer user_data);
static void monkey_media_cd_playback_mode_changed (GConfClient *client, guint cnxn_id,
			                           GConfEntry *entry, gpointer user_data);
static void popt_callback (poptContext context, enum poptCallbackReason reason,
	                   const struct poptOption *option, const char *arg, gpointer data);

static GConfClient *gconf_client = NULL;

static char *audio_driver = NULL;
static char *cd_drive = NULL;
static MonkeyMediaCDPlaybackMode cd_playback_mode = MONKEY_MEDIA_CD_PLAYBACK_NO_ERROR_CORRECTION;

static char *mmdir = NULL;

static GMainLoop *main_loop = NULL;

static gboolean alive = FALSE;

typedef struct
{
	char *mime_type;
	GType stream_info_impl_type;
} Impl;

static GPtrArray *impl_array;

#define CONF_DIR                  "/system/monkey_media"
#define CONF_KEY_AUDIO_DRIVER     "/system/monkey_media/audio_driver"
#define CONF_KEY_CD_DRIVE         "/system/monkey_media/cd_drive"
#define CONF_KEY_CD_PLAYBACK_MODE "/system/monkey_media/cd_playback_mode"

void
monkey_media_init (int *argc, char ***argv)
{
	if (monkey_media_is_alive () == TRUE) return;

#ifdef HAVE_GSTREAMER
	gst_init (argc, argv);
	gst_control_init (argc, argv);
#endif

	monkey_media_init_internal ();
}

void
nonkey_media_init_with_popt_table (int *argc, char ***argv,
	                           const struct poptOption *popt_options)
{
	if (monkey_media_is_alive () == TRUE) return;

#ifdef HAVE_GSTREAMER
	gst_init_with_popt_table (argc, argv, popt_options);
	gst_control_init (argc, argv);
#endif

	monkey_media_init_internal ();
}

static void
register_type (const char *mime_type,
	       GType stream_info_impl_type)
{
	Impl *impl;

	impl = g_new0 (Impl, 1);

	impl->mime_type             = g_strdup (mime_type);
	impl->stream_info_impl_type = stream_info_impl_type;

	g_ptr_array_add (impl_array, impl);
}

static void
monkey_media_init_internal (void)
{
	g_type_init ();

#ifdef ENABLE_NLS
	/* initialize i18n */
	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);
#endif

	/* initialize gconf if necessary */
	gconf_client = gconf_client_get_default ();
	gconf_client_add_dir (gconf_client,
			      CONF_DIR,
			      GCONF_CLIENT_PRELOAD_NONE,
			      NULL);

	gconf_client_notify_add (gconf_client,
				 CONF_KEY_AUDIO_DRIVER,
				 monkey_media_audio_driver_changed,
				 NULL, NULL, NULL);
	gconf_client_notify_add (gconf_client,
				 CONF_KEY_CD_DRIVE,
				 monkey_media_cd_drive_changed,
				 NULL, NULL, NULL);
	gconf_client_notify_add (gconf_client,
				 CONF_KEY_CD_PLAYBACK_MODE,
				 monkey_media_cd_playback_mode_changed,
				 NULL, NULL, NULL);

	audio_driver = gconf_client_get_string (gconf_client,
					        CONF_KEY_AUDIO_DRIVER,
					        NULL);
	cd_drive = gconf_client_get_string (gconf_client,
					    CONF_KEY_CD_DRIVE,
					    NULL);
	cd_playback_mode = gconf_client_get_int (gconf_client,
						 CONF_KEY_CD_PLAYBACK_MODE,
						 NULL);

	/* initialize gnome-vfs if necessary */
	if (gnome_vfs_initialized () == FALSE)
		gnome_vfs_init ();

	/* register mimetypes */
	impl_array = g_ptr_array_new ();
#ifdef HAVE_VORBIS
	register_type ("application/ogg",
		       TYPE_VORBIS_STREAM_INFO_IMPL);
	register_type ("application/x-ogg",
		       TYPE_VORBIS_STREAM_INFO_IMPL);
#endif
#ifdef HAVE_FLAC
        register_type ("application/x-flac",
                       TYPE_FLAC_STREAM_INFO_IMPL);
        register_type ("audio/x-flac",
                       TYPE_FLAC_STREAM_INFO_IMPL);
#endif
#ifdef HAVE_MP3
	register_type ("audio/x-mp3",
		       TYPE_MP3_STREAM_INFO_IMPL);
	register_type ("audio/mpeg",
		       TYPE_MP3_STREAM_INFO_IMPL);
	register_type ("audio/x-wav",
		       TYPE_MP3_STREAM_INFO_IMPL);
#endif
#ifdef HAVE_MP4
	register_type ("audio/x-m4a",
		       TYPE_MP4_STREAM_INFO_IMPL);
#endif
#ifdef HAVE_AUDIOCD
	register_type ("audiocd",
		       TYPE_AUDIOCD_STREAM_INFO_IMPL);
#endif

	/* be sure dir exists */
	mmdir = g_build_filename (g_get_home_dir (),
				  ".gnome2",
				  "monkey-media",
				  NULL);

	monkey_media_mkdir (mmdir);

	/* okay, we're alive */
	alive = TRUE;
}

const struct poptOption *
monkey_media_get_popt_table (void)
{
	static struct poptOption options[] =
	{
		{ NULL, '\0', POPT_ARG_CALLBACK | POPT_CBFLAG_POST, &popt_callback, 0, NULL,                     NULL },
#ifdef HAVE_GSTREAMER
		{ NULL, '\0', POPT_ARG_INCLUDE_TABLE,               NULL,           0, N_("GStreamer options:"), NULL },
#endif
		POPT_TABLEEND
	};

#ifdef HAVE_GSTREAMER
	options[1].arg = (poptOption *) gst_init_get_popt_table ();
#endif

	return options;
}

GList *
monkey_media_get_supported_filename_extensions (void)
{
	GList *ret = NULL;
	int i;

	for (i = 0; i < impl_array->len; i++) {
		Impl *impl = g_ptr_array_index (impl_array, i);
		GList *types = gnome_vfs_mime_get_extensions_list (impl->mime_type);
		GList *tem;

		for (tem = types; tem != NULL; tem = g_list_next (tem))
			ret = g_list_append (ret, tem->data);

		g_list_free (types);
	}

	return ret;
}

void
monkey_media_shutdown (void)
{
	int i;

	if (monkey_media_is_alive () == FALSE)
		return;

	alive = FALSE;

#ifdef HAVE_AUDIOCD
	monkey_media_audio_cd_unref_if_around ();
#endif	
#ifdef HAVE_MUSICBRAINZ
	monkey_media_musicbrainz_unref_if_around ();
#endif

	g_free (cd_drive);

	g_object_unref (G_OBJECT (gconf_client));

	for (i = 0; i < impl_array->len; i++)
	{
		Impl *impl;

		impl = g_ptr_array_index (impl_array, i);

		g_free (impl->mime_type);
		g_free (impl);
	}
	g_ptr_array_free (impl_array, FALSE);

	gnome_vfs_shutdown ();

	g_free (mmdir);
}

void
monkey_media_main (void)
{
	g_assert (main_loop == NULL);

	main_loop = g_main_loop_new (NULL, FALSE);

	g_main_loop_run (main_loop);
}

void
monkey_media_main_quit (void)
{
	g_assert (main_loop != NULL);

	g_main_loop_quit (main_loop);
}

const char *
monkey_media_get_audio_driver (void)
{
	const char *ret = (const char *) audio_driver;

	g_return_val_if_fail (alive == TRUE, NULL);

	/* we default to auto */
	if (ret == NULL)
		ret = "auto";

	return ret;
}

void
monkey_media_set_audio_driver (const char *audio_driver)
{
	g_return_if_fail (alive == TRUE);

	g_return_if_fail (gconf_client != NULL);

	gconf_client_set_string (gconf_client,
				 CONF_KEY_AUDIO_DRIVER,
				 audio_driver,
				 NULL);
}

static void
monkey_media_audio_driver_changed (GConfClient *client, guint cnxn_id,
			           GConfEntry *entry, gpointer user_data)
{
	g_free (audio_driver);

	audio_driver = gconf_client_get_string (gconf_client,
					        CONF_KEY_AUDIO_DRIVER,
					        NULL);
}

const char *
monkey_media_get_cd_drive (void)
{
	const char *ret = (const char *) cd_drive;

	g_return_val_if_fail (alive == TRUE, NULL);

	/* we default to osssink */
	if (ret == NULL)
	{
		g_warning ("Could not find value for " CONF_KEY_CD_DRIVE ", this means that either monkey-media"
			   "is not installed properly or your GConf setup is broken.  Defaulting to /dev/cdrom.");

		ret = "/dev/cdrom";
	}

	return ret;
}

void
monkey_media_set_cd_drive (const char *cd_drive)
{
	g_return_if_fail (alive == TRUE);

	g_return_if_fail (gconf_client != NULL);

	gconf_client_set_string (gconf_client,
				 CONF_KEY_CD_DRIVE,
				 cd_drive,
				 NULL);
}

static void
monkey_media_cd_drive_changed (GConfClient *client, guint cnxn_id,
			       GConfEntry *entry, gpointer user_data)
{
	g_free (cd_drive);

	cd_drive = gconf_client_get_string (gconf_client,
					    CONF_KEY_CD_DRIVE,
					    NULL);
}

MonkeyMediaCDPlaybackMode
monkey_media_get_cd_playback_mode (void)
{
	g_return_val_if_fail (alive == TRUE, MONKEY_MEDIA_CD_PLAYBACK_NO_ERROR_CORRECTION);

	return cd_playback_mode;
}

void
monkey_media_set_cd_playback_mode (MonkeyMediaCDPlaybackMode playback_mode)
{
	g_return_if_fail (alive == TRUE);

	g_return_if_fail (gconf_client != NULL);

	gconf_client_set_int (gconf_client,
			      CONF_KEY_CD_PLAYBACK_MODE,
			      playback_mode,
			      NULL);
}

static void
monkey_media_cd_playback_mode_changed (GConfClient *client, guint cnxn_id,
			               GConfEntry *entry, gpointer user_data)
{
	cd_playback_mode = gconf_client_get_int (gconf_client,
					         CONF_KEY_CD_PLAYBACK_MODE,
					         NULL);
}

static void
popt_callback (poptContext context, enum poptCallbackReason reason,
	       const struct poptOption *option, const char *arg, gpointer data)
{
	switch (reason)
	{
	case POPT_CALLBACK_REASON_POST:
		if (monkey_media_is_alive () == TRUE) return;

#ifdef HAVE_GSTREAMER
		gst_control_init (NULL, NULL);
#endif
		monkey_media_init_internal ();
		break;
	default:
		break;
	}
}

gboolean
monkey_media_is_alive (void)
{
	return alive;
}

static Impl *
monkey_media_get_impl_for (const char *uri, char **mimetype)
{
	Impl *ret = NULL;
	int i;

	if (strncmp (uri, "audiocd://", 10) == 0)
		*mimetype = g_strdup ("audiocd");
	else
		*mimetype = gnome_vfs_get_mime_type (uri);

	if (*mimetype == NULL)
		return FALSE;

	for (i = 0; i < impl_array->len; i++)
	{
		Impl *impl;

		impl = g_ptr_array_index (impl_array, i);

		if (strcmp (impl->mime_type, *mimetype) == 0)
		{
			ret = impl;
			break;
		}
	}

	return ret;
}

GType
monkey_media_get_stream_info_impl_for (const char *uri, char **mimetype)
{
	Impl *impl;

	impl = monkey_media_get_impl_for (uri, mimetype);

	if (impl == NULL)
		return -1;

	return impl->stream_info_impl_type;
}

const char *
monkey_media_get_dir (void)
{
	return (const char *) mmdir;
}

void
monkey_media_mkdir (const char *path)
{
	if (g_file_test (path, G_FILE_TEST_IS_DIR) == FALSE) {
		if (g_file_test (path, G_FILE_TEST_EXISTS) == TRUE)
			g_warning (_("Please remove %s"), path);
		else if (mkdir (path, 488) != 0)
			g_warning (_("Failed to create directory %s"), path);
	}
}
