/*  monkey-media
 *
 *  arch-tag: Implementation of MonkeyMedia AudioCD playback object
 *
 *  Copyright (C) 2001 Iain Holmes <iain@ximian.com>
 *                2002 Kenneth Christiansen <kenneth@gnu.org>
 *                     Olivier Martin <omartin@ifrance.com>
 *                     Jorn Baayen <jorn@nl.linux.org>
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
#include <sys/types.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <linux/cdrom.h>

#include "monkey-media.h"
#include "monkey-media-private.h"
#include "sha1.h"

static void monkey_media_audio_cd_class_init (MonkeyMediaAudioCDClass *klass);
static void monkey_media_audio_cd_init (MonkeyMediaAudioCD *cd);
static void monkey_media_audio_cd_finalize (GObject *object);
static void monkey_media_audio_cd_set_property (GObject *object,
				                guint prop_id,
				                const GValue *value,
				                GParamSpec *pspec);
static void monkey_media_audio_cd_get_property (GObject *object,
				                guint prop_id,
				                GValue *value,
				                GParamSpec *pspec);
static gboolean poll_event_cb (MonkeyMediaAudioCD *cd);
      
struct MonkeyMediaAudioCDPrivate 
{
	int open_count;

        int poll_func_id;

	GError *error;

	int fd;

        gboolean valid_info;

	gboolean cd_available;
	
        int n_audio_tracks;
        int *track_lengths;
        int *track_offsets;

	char *cd_id;

	GMutex *lock;
};

enum
{
	PROP_0,
	PROP_ERROR
};

enum
{
	CD_CHANGED,
	LAST_SIGNAL
};

static guint monkey_media_audio_cd_signals[LAST_SIGNAL] = { 0 };

static GObjectClass *parent_class = NULL;

static MonkeyMediaAudioCD *global_cd = NULL;
static GMutex *global_cd_lock = NULL;

GType
monkey_media_audio_cd_get_type (void)
{
	static GType type = 0;

	if (type == 0) 
	{
		static const GTypeInfo our_info =
		{
			sizeof (MonkeyMediaAudioCDClass),
			NULL,
			NULL,
			(GClassInitFunc) monkey_media_audio_cd_class_init,
			NULL,
			NULL,
			sizeof (MonkeyMediaAudioCD),
			0,
			(GInstanceInitFunc) monkey_media_audio_cd_init,
		};

		type = g_type_register_static (G_TYPE_OBJECT,
					       "MonkeyMediaAudioCD",
					       &our_info, 0);
	}

	return type;
}

static void
monkey_media_audio_cd_class_init (MonkeyMediaAudioCDClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = monkey_media_audio_cd_finalize;

	object_class->set_property = monkey_media_audio_cd_set_property;
	object_class->get_property = monkey_media_audio_cd_get_property;

	g_object_class_install_property (object_class,
				         PROP_ERROR,
				         g_param_spec_pointer ("error",
							       "Error",
							       "Failure information",
							        G_PARAM_READABLE));

	monkey_media_audio_cd_signals[CD_CHANGED] =
		g_signal_new ("cd_changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (MonkeyMediaAudioCDClass, cd_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_BOOLEAN);
}


static gboolean
is_cdrom_device (MonkeyMediaAudioCD *cdrom)
{
	int fd;

	fd = open (monkey_media_get_cd_drive (), O_RDONLY | O_NONBLOCK);
	if (fd < 0) 
	{
		return FALSE;
	}

	/* Fire a harmless ioctl at the device. */
	if (ioctl (fd, CDROM_GET_CAPABILITY, 0) < 0)
	{
		/* Failed, it's not a CDROM drive */
		close (fd);
		
		return FALSE;
	}
	
	close (fd);

	return TRUE;
}

gboolean
monkey_media_audio_cd_device_available ()
{
	int fd;

	fd = open (monkey_media_get_cd_drive (), O_RDONLY | O_NONBLOCK);
	if (fd < 0) 
	{
		return FALSE;
	}

	/* Fire a harmless ioctl at the device. */
	if (ioctl (fd, CDROM_GET_CAPABILITY, 0) < 0)
	{
		/* Failed, it's not a CDROM drive */
		close (fd);
		
		return FALSE;
	}
	
	close (fd);

	return TRUE;
}


static void
monkey_media_audio_cd_init (MonkeyMediaAudioCD *cd)
{
	cd->priv = g_new0 (MonkeyMediaAudioCDPrivate, 1);

	cd->priv->lock = g_mutex_new ();

	if (!is_cdrom_device (cd))
	{
		cd->priv->error = g_error_new (MONKEY_MEDIA_AUDIO_CD_ERROR,
					       MONKEY_MEDIA_AUDIO_CD_ERROR_NOT_OPENED,
					       _("%s does not point to a valid CDRom device. This may be caused by:\n"
					         "a) CD support is not compiled into Linux\n"
					         "b) You do not have the correct permissions to access the CD drive\n"
					         "c) %s is not the CD drive.\n"),
					       monkey_media_get_cd_drive (), monkey_media_get_cd_drive ());
		return;
	}

        cd->priv->poll_func_id = g_timeout_add (1000, (GSourceFunc) poll_event_cb, cd);

	cd->priv->valid_info = FALSE;
	cd->priv->cd_available = monkey_media_audio_cd_available (cd, NULL);
}

static void
monkey_media_audio_cd_finalize (GObject *object)
{
	MonkeyMediaAudioCD *cd;

	cd = MONKEY_MEDIA_AUDIO_CD (object);

	g_mutex_free (cd->priv->lock);

	g_source_remove (cd->priv->poll_func_id);

	g_free (cd->priv->cd_id);
	g_free (cd->priv->track_lengths);
	g_free (cd->priv->track_offsets);

	g_free (cd->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
monkey_media_audio_cd_set_property (GObject *object,
				    guint prop_id,
				    const GValue *value,
				    GParamSpec *pspec)
{
	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
monkey_media_audio_cd_get_property (GObject *object,
				    guint prop_id,
				    GValue *value,
				    GParamSpec *pspec)
{
	MonkeyMediaAudioCD *cd = MONKEY_MEDIA_AUDIO_CD (object);

	if (prop_id != PROP_ERROR)
	{
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		return;
	}

	g_value_set_pointer (value, cd->priv->error);
}

void
monkey_media_audio_cd_unref_if_around (void)
{
	if (global_cd != NULL)
		g_object_unref (G_OBJECT (global_cd));
	if (global_cd_lock != NULL)
		g_mutex_free (global_cd_lock);
}

MonkeyMediaAudioCD *
monkey_media_audio_cd_new (GError **error)
{
	MonkeyMediaAudioCD *cd;
	GError *tmp;

	if (global_cd_lock == NULL)
		global_cd_lock = g_mutex_new ();
	g_mutex_lock (global_cd_lock);

	/* we fake creating the object, since we need it internally as well
	 * and having multiple instances would just be a waste of memory. */
	if (global_cd != NULL)
	{
		g_mutex_unlock (global_cd_lock);
		return g_object_ref (G_OBJECT (global_cd));
	}

	cd = MONKEY_MEDIA_AUDIO_CD (g_object_new (MONKEY_MEDIA_TYPE_AUDIO_CD, NULL));

	g_return_val_if_fail (cd->priv != NULL, NULL);

	g_object_get (G_OBJECT (cd), "error", &tmp, NULL);

	if (tmp != NULL)
	{
		if (error != NULL)
			*error = tmp;
		else
			g_error_free (tmp);

		g_object_unref (G_OBJECT (cd));
		cd = NULL;
	}

	global_cd = cd;

	g_mutex_unlock (global_cd_lock);

	return g_object_ref (G_OBJECT (cd));
}

static gboolean
monkey_media_audio_cd_open (MonkeyMediaAudioCD *cd,
			    GError **error)
{
	if (cd->priv->open_count++ == 0) 
	{
		cd->priv->fd = open (monkey_media_get_cd_drive (), O_RDONLY | O_NONBLOCK);
		
		if (cd->priv->fd < 0) 
		{
			if (errno == EACCES && error != NULL)
			{
				*error = g_error_new (MONKEY_MEDIA_AUDIO_CD_ERROR,
					              MONKEY_MEDIA_AUDIO_CD_ERROR_NOT_OPENED,
					              _("You do not seem to have permission to access %s."),
					              monkey_media_get_cd_drive ());
			} 
			else if (error != NULL) 
			{
				*error = g_error_new (MONKEY_MEDIA_AUDIO_CD_ERROR,
					              MONKEY_MEDIA_AUDIO_CD_ERROR_NOT_OPENED,
					              _("%s does not appear to point to a valid CD device. This may be because:\n"
					                "a) CD support is not compiled into Linux\n"
					                "b) You do not have the correct permissions to access the CD drive\n"
					                "c) %s is not the CD drive.\n"),
					              monkey_media_get_cd_drive (),
					              monkey_media_get_cd_drive ());
			}
				
			cd->priv->open_count = 0;
			return FALSE;
		}
	}
	
	return (cd->priv->fd >= 0);
}

static void
monkey_media_audio_cd_close (MonkeyMediaAudioCD *cd, 
			     gboolean force_close)
{
	if (--cd->priv->open_count < 0 || force_close)
	{
		cd->priv->open_count = 0;
	}

	if (cd->priv->open_count == 0) 
	{
		if (cd->priv->fd >= 0)
			close (cd->priv->fd);
		cd->priv->fd = -1;
	}
}

void
monkey_media_audio_cd_open_tray (MonkeyMediaAudioCD *cd,
			         GError **error)
{
	int cd_status;

	g_return_if_fail (MONKEY_MEDIA_IS_AUDIO_CD (cd));

	g_mutex_lock (cd->priv->lock);

	if (monkey_media_audio_cd_open (cd, error) == FALSE) 
	{
		g_mutex_unlock (cd->priv->lock);
		return;
	}

	cd_status = ioctl (cd->priv->fd, CDROM_DRIVE_STATUS, CDSL_CURRENT);

	if (cd_status != -1) 
	{
		if (cd_status != CDS_TRAY_OPEN)
		{
			if (ioctl (cd->priv->fd, CDROMEJECT, 0) < 0) 
			{
				if (error != NULL)
				{
					*error = g_error_new (MONKEY_MEDIA_AUDIO_CD_ERROR,
							      MONKEY_MEDIA_AUDIO_CD_ERROR_SYSTEM_ERROR,
							      "(monkey_media_audio_cd_open_tray): ioctl failed: %s",
							      g_strerror (errno));
				}
				
				monkey_media_audio_cd_close (cd, FALSE);

				g_mutex_unlock (cd->priv->lock);

				return;
			}
		} 
	}

	monkey_media_audio_cd_close (cd, TRUE);

	g_mutex_unlock (cd->priv->lock);
}

void
monkey_media_audio_cd_close_tray (MonkeyMediaAudioCD *cd,
				  GError **error)
{
	g_return_if_fail (MONKEY_MEDIA_IS_AUDIO_CD (cd));

	g_mutex_lock (cd->priv->lock);

	if (monkey_media_audio_cd_open (cd, error) == FALSE) 
	{
		g_mutex_unlock (cd->priv->lock);
		return;
	}

	if (ioctl (cd->priv->fd, CDROMCLOSETRAY) < 0) 
	{
		if (error != NULL) 
		{
			*error = g_error_new (MONKEY_MEDIA_AUDIO_CD_ERROR,
					      MONKEY_MEDIA_AUDIO_CD_ERROR_SYSTEM_ERROR,
					      "(monkey_media_audio_cd_close_tray): ioctl failed %s",
					      g_strerror (errno));
		}

		monkey_media_audio_cd_close (cd, FALSE);
		
		g_mutex_unlock (cd->priv->lock);
		
		return;
	}

	monkey_media_audio_cd_close (cd, TRUE);

	g_mutex_unlock (cd->priv->lock);
}

gboolean
monkey_media_audio_cd_available (MonkeyMediaAudioCD *cd,
				 GError **error)
{
	int cd_status;
	gboolean ret;

	g_return_val_if_fail (MONKEY_MEDIA_IS_AUDIO_CD (cd), FALSE);

	g_mutex_lock (cd->priv->lock);

	if (monkey_media_audio_cd_open (cd, error) == FALSE) 
	{
		g_mutex_unlock (cd->priv->lock);
		return FALSE;
	}

	cd_status = ioctl (cd->priv->fd, CDROM_DRIVE_STATUS, CDSL_CURRENT);

	monkey_media_audio_cd_close (cd, TRUE);

	ret = (cd_status == CDS_DISC_OK);

	g_mutex_unlock (cd->priv->lock);

	return ret;
}

/*
 * Program:	RFC-822 routines (originally from SMTP)
 *
 * Author:	Mark Crispin
 *		Networks and Distributed Computing
 *		Computing & Communications
 *		University of Washington
 *		Administration Building, AG-44
 *		Seattle, WA  98195
 *		Internet: MRC@CAC.Washington.EDU
 *
 * Date:	27 July 1988
 * Last Edited:	10 September 1998
 *
 * Sponsorship:	The original version of this work was developed in the
 *		Symbolic Systems Resources Group of the Knowledge Systems
 *		Laboratory at Stanford University in 1987-88, and was funded
 *		by the Biomedical Research Technology Program of the National
 *		Institutes of Health under grant number RR-00785.
 *
 * Original version Copyright 1988 by The Leland Stanford Junior University
 * Copyright 1998 by the University of Washington
 *
 *  Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose and without fee is hereby granted, provided
 * that the above copyright notices appear in all copies and that both the
 * above copyright notices and this permission notice appear in supporting
 * documentation, and that the name of the University of Washington or The
 * Leland Stanford Junior University not be used in advertising or publicity
 * pertaining to distribution of the software without specific, written prior
 * permission.  This software is made available "as is", and
 * THE UNIVERSITY OF WASHINGTON AND THE LELAND STANFORD JUNIOR UNIVERSITY
 * DISCLAIM ALL WARRANTIES, EXPRESS OR IMPLIED, WITH REGARD TO THIS SOFTWARE,
 * INCLUDING WITHOUT LIMITATION ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE, AND IN NO EVENT SHALL THE UNIVERSITY OF
 * WASHINGTON OR THE LELAND STANFORD JUNIOR UNIVERSITY BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, TORT (INCLUDING NEGLIGENCE) OR STRICT LIABILITY, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

/* NOTE: This is not true RFC822 anymore. The use of the characters
   '/', '+', and '=' is no bueno when the ID will be used as part of a URL.
   '_', '.', and '-' have been used instead
*/

/* Convert binary contents to BASE64
 * Accepts: source
 *	    length of source
 *	    pointer to return destination length
 * Returns: destination as BASE64
 */

unsigned char *rfc822_binary (void *src,unsigned long srcl,unsigned long *len)
{
  unsigned char *ret,*d;
  unsigned char *s = (unsigned char *) src;
  char *v = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._";
  unsigned long i = ((srcl + 2) / 3) * 4;
  *len = i += 2 * ((i / 60) + 1);
  d = ret = (unsigned char *) malloc ((size_t) ++i);
  for (i = 0; srcl; s += 3) {	/* process tuplets */
    *d++ = v[s[0] >> 2];	/* byte 1: high 6 bits (1) */
				/* byte 2: low 2 bits (1), high 4 bits (2) */
    *d++ = v[((s[0] << 4) + (--srcl ? (s[1] >> 4) : 0)) & 0x3f];
				/* byte 3: low 4 bits (2), high 2 bits (3) */
    *d++ = srcl ? v[((s[1] << 2) + (--srcl ? (s[2] >> 6) : 0)) & 0x3f] : '-';
				/* byte 4: low 6 bits (3) */
    *d++ = srcl ? v[s[2] & 0x3f] : '-';
    if (srcl) srcl--;		/* count third character if processed */
    if ((++i) == 15) {		/* output 60 characters? */
      i = 0;			/* restart line break count, insert CRLF */
      *d++ = '\015'; *d++ = '\012';
    }
  }
  *d = '\0';			/* tie off string */

  return ret;			/* return the resulting string */
}

static gboolean
ensure_sync (MonkeyMediaAudioCD *cd,
	     GError **error)
{
	int i, j, *track_frames;
	struct cdrom_tochdr tochdr;
	unsigned char track0, track1;
	struct cdrom_tocentry tocentry;
	long *frame_offsets;
	SHA_INFO sha;
	char *tmp;
	unsigned char digest[20], *base64;
	unsigned long size;
 
        /* Don't recalculate data, valid_data can be changed from
         * callback functions */
        if (cd->priv->valid_info == TRUE)
		return TRUE;

	if (monkey_media_audio_cd_open (cd, error) == FALSE) 
		return FALSE;
	
	if (ioctl (cd->priv->fd, CDROMREADTOCHDR, &tochdr) < 0) 
	{
		if (error != NULL)
		{
			*error = g_error_new (MONKEY_MEDIA_AUDIO_CD_ERROR,
					      MONKEY_MEDIA_AUDIO_CD_ERROR_SYSTEM_ERROR,
					      _("Error reading CD header: %s"),
					      g_strerror (errno));
		}
		monkey_media_audio_cd_close (cd, FALSE);
		return FALSE;
	}
	
	track0 = tochdr.cdth_trk0;
	track1 = tochdr.cdth_trk1;
	cd->priv->n_audio_tracks = track1 - track0 + 1;

        if (cd->priv->track_offsets != NULL)
        {
                g_free (cd->priv->track_offsets);
                g_free (cd->priv->track_lengths);
        }

	cd->priv->track_offsets = g_new0 (int, cd->priv->n_audio_tracks + 1);
        cd->priv->track_lengths = g_new0 (int, cd->priv->n_audio_tracks + 1);

	frame_offsets = g_new0 (long, 100);
	track_frames = g_new0 (int, cd->priv->n_audio_tracks + 1);

	for (i = 0, j = track0; i < cd->priv->n_audio_tracks; i++, j++) 
	{
		/* handle time-based stuff */
		tocentry.cdte_track = j;
		tocentry.cdte_format = CDROM_MSF;

		if (ioctl (cd->priv->fd, CDROMREADTOCENTRY, &tocentry) < 0) 
		{
			g_warning ("IOCtl failed");
			continue;
		}

		cd->priv->track_offsets[i] = (tocentry.cdte_addr.msf.minute * 60) 
                        + tocentry.cdte_addr.msf.second;
		track_frames[i] = tocentry.cdte_addr.msf.frame;

		/* get frame offest */
		tocentry.cdte_track = j;
		tocentry.cdte_format = CDROM_LBA;

		if (ioctl (cd->priv->fd, CDROMREADTOCENTRY, &tocentry) < 0) 
		{
			g_warning ("IOCtl failed");
			continue;
		}
		
		frame_offsets[i + 1] = tocentry.cdte_addr.lba + 150;
	}

	/* handle time based stuff */
	tocentry.cdte_track = CDROM_LEADOUT;
	tocentry.cdte_format = CDROM_MSF;
	
	if (ioctl (cd->priv->fd, CDROMREADTOCENTRY, &tocentry) < 0) 
		goto leadout_error;
	
	cd->priv->track_offsets[cd->priv->n_audio_tracks] = (tocentry.cdte_addr.msf.minute * 60) 
                + tocentry.cdte_addr.msf.second;
	track_frames[cd->priv->n_audio_tracks] = tocentry.cdte_addr.msf.frame;

	/* get frame offset */
	tocentry.cdte_track = CDROM_LEADOUT;
	tocentry.cdte_format = CDROM_LBA;

	if (ioctl (cd->priv->fd, CDROMREADTOCENTRY, &tocentry) < 0)
		goto leadout_error;
	
	frame_offsets[0] = tocentry.cdte_addr.lba + 150;

        for (i = 0; i < cd->priv->n_audio_tracks; i++) 
        {
                int f1, f2, df;
                
                /* Convert all addresses to frames */
                f1 = cd->priv->track_offsets[i] * CD_FRAMES + track_frames[i];
                f2 = cd->priv->track_offsets[i + 1] * CD_FRAMES + track_frames[i + 1];
                
                df = f2 - f1;
                cd->priv->track_lengths[i] = df / CD_FRAMES;
        }

	/* calculates the musicbrainz disc ID. We do it locally instead of calling
	 * the lib for this, since we avoid the cd to be accessed yet another time
	 * this way */
	sha_init (&sha);

	tmp = g_strdup_printf ("%02X", track0);
	sha_update (&sha, (unsigned char *) tmp, strlen (tmp));
	g_free (tmp);

	tmp = g_strdup_printf ("%02X", track1);
	sha_update (&sha, (unsigned char *) tmp, strlen (tmp));
	g_free (tmp);
	
	for (i = 0; i < 100; i++)
	{
		tmp = g_strdup_printf ("%08lX", frame_offsets[i]);
		sha_update (&sha, (unsigned char *) tmp, strlen (tmp));
		g_free (tmp);
	}

	sha_final (digest, &sha);

	base64 = rfc822_binary (digest, 20, &size);
	if (cd->priv->cd_id != NULL)
		g_free (cd->priv->cd_id);
	cd->priv->cd_id = g_strndup (base64, size);
	g_free (base64);

	g_free (track_frames);
	g_free (frame_offsets);

	monkey_media_audio_cd_close (cd, TRUE);

        cd->priv->valid_info = TRUE;

	return TRUE;

leadout_error:
	if (error != NULL)
	{
		*error = g_error_new (MONKEY_MEDIA_AUDIO_CD_ERROR,
				      MONKEY_MEDIA_AUDIO_CD_ERROR_SYSTEM_ERROR,
				      _("Error getting leadout: %s"),
				      g_strerror (errno));
	}

	monkey_media_audio_cd_close (cd, FALSE);

	g_free (track_frames);
	g_free (frame_offsets);

	return FALSE;
}


static gboolean
poll_event_cb (MonkeyMediaAudioCD *cd)
{
	int cd_status;
	gboolean emit_signal = FALSE, available = FALSE;

	g_mutex_lock (cd->priv->lock);

	if (monkey_media_audio_cd_open (cd, NULL) == FALSE)
	{
		cd->priv->valid_info = FALSE;
		cd->priv->cd_available = FALSE;

		g_mutex_unlock (cd->priv->lock);

		return TRUE;
	}
	
	cd_status = ioctl (cd->priv->fd, CDROM_DRIVE_STATUS, CDSL_CURRENT);
	if (cd_status != -1) 
	{
		switch (cd_status) 
		{
		case CDS_NO_INFO:
		case CDS_NO_DISC:
		case CDS_TRAY_OPEN:
		case CDS_DRIVE_NOT_READY:
			if (cd->priv->cd_available == TRUE)
			{
				cd->priv->cd_available = FALSE;
				cd->priv->valid_info = FALSE;

				emit_signal = TRUE;
				available = FALSE;
			}
                        break;
		default:
			if (cd->priv->cd_available == FALSE)
			{
				cd->priv->cd_available = TRUE;
				cd->priv->valid_info = FALSE;

				emit_signal = TRUE;
				available = TRUE;
                        }
			break;
		}
	} 
	else
	{
		/* so we went out of sync.. */
		cd->priv->valid_info = FALSE;
	}

	monkey_media_audio_cd_close (cd, FALSE);

	g_mutex_unlock (cd->priv->lock);

	if (emit_signal == TRUE)
	{
		g_signal_emit (G_OBJECT (cd), monkey_media_audio_cd_signals[CD_CHANGED], 0, available);
	}

	return TRUE;
}

char *
monkey_media_audio_cd_get_disc_id (MonkeyMediaAudioCD *cd,
				   GError **error)
{
	char *ret;

	g_return_val_if_fail (MONKEY_MEDIA_IS_AUDIO_CD (cd), NULL);

	g_mutex_lock (cd->priv->lock);

	if (ensure_sync (cd, error) == FALSE)
	{
		g_mutex_unlock (cd->priv->lock);
		return NULL;
	}

	ret = g_strdup (cd->priv->cd_id);

	g_mutex_unlock (cd->priv->lock);

	return ret;
}

long
monkey_media_audio_cd_get_track_duration (MonkeyMediaAudioCD *cd,
					  int track,
					  GError **error)
{
	long ret;

	g_return_val_if_fail (MONKEY_MEDIA_IS_AUDIO_CD (cd), -1);

	g_mutex_lock (cd->priv->lock);

	if (ensure_sync (cd, error) == FALSE)
	{
		g_mutex_unlock (cd->priv->lock);
		return -1;
	}

	g_return_val_if_fail (track > 0 && track <= cd->priv->n_audio_tracks, -1);

	ret = (long) cd->priv->track_lengths[track - 1];

	g_mutex_unlock (cd->priv->lock);

	return ret;
}

int
monkey_media_audio_cd_get_track_offset (MonkeyMediaAudioCD *cd,
					int track,
					GError **error)
{
	int ret;
	
	g_return_val_if_fail (MONKEY_MEDIA_IS_AUDIO_CD (cd), -1);

	g_mutex_lock (cd->priv->lock);

	if (ensure_sync (cd, error) == FALSE)
	{
		g_mutex_unlock (cd->priv->lock);
		return -1;
	}

	g_return_val_if_fail (track > 0 && track <= cd->priv->n_audio_tracks, -1);

	ret = cd->priv->track_offsets[track - 1];

	g_mutex_unlock (cd->priv->lock);

	return ret;
}

gboolean
monkey_media_audio_cd_have_track (MonkeyMediaAudioCD *cd,
				  int track,
				  GError **error)
{
	gboolean ret;
	
	g_return_val_if_fail (MONKEY_MEDIA_IS_AUDIO_CD (cd), FALSE);

	g_mutex_lock (cd->priv->lock);

	if (ensure_sync (cd, error) == FALSE)
	{
		g_mutex_unlock (cd->priv->lock);
		return FALSE;
	}

	ret = (track > 0 && track <= cd->priv->n_audio_tracks);

	g_mutex_unlock (cd->priv->lock);

	return ret;
}

int
monkey_media_audio_cd_get_n_tracks (MonkeyMediaAudioCD *cd,
				    GError **error)
{
	int ret;

	g_return_val_if_fail (MONKEY_MEDIA_IS_AUDIO_CD (cd), -1);
	
	g_mutex_lock (cd->priv->lock);

	if (ensure_sync (cd, error) == FALSE)
	{
		g_mutex_unlock (cd->priv->lock);
		return -1;
	}

	ret = cd->priv->n_audio_tracks;

	g_mutex_unlock (cd->priv->lock);

	return ret;
}

GList *
monkey_media_audio_cd_list_tracks (MonkeyMediaAudioCD *cd,
				   GError **error)
{
	GList *ret = NULL;
	int i;

	g_return_val_if_fail (MONKEY_MEDIA_IS_AUDIO_CD (cd), NULL);

	g_mutex_lock (cd->priv->lock);

	if (ensure_sync (cd, error) == FALSE)
	{
		g_mutex_unlock (cd->priv->lock);
		return NULL;
	}

	for (i = 0; i < cd->priv->n_audio_tracks; i++)
	{
		char *uri;

		uri = g_strdup_printf ("audiocd://%d", i + 1);

		ret = g_list_append (ret, uri);
	}

	g_mutex_unlock (cd->priv->lock);

	return ret;
}

void
monkey_media_audio_cd_free_tracks (GList *list)
{
	GList *l;
	
	for (l = list; l != NULL; l = g_list_next (l))
	{
		g_free (l->data);
	}

	g_list_free (list);
}

GQuark
monkey_media_audio_cd_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark) 
		quark = g_quark_from_static_string ("monkey_media_audio_cd_error");

	return quark;
}
