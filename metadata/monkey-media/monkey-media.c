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
	for (i = 0; i < impl_array->len; i++)
	{
		Impl *impl;

		impl = g_ptr_array_index (impl_array, i);

		g_free (impl->mime_type);
		g_free (impl);
	}
	g_ptr_array_free (impl_array, FALSE);
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
