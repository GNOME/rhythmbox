/*  monkey-sound
 *
 *  arch-tag: Header for AudioCD metadata loading
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

#ifndef __AUDIOCD_STREAM_INFO_IMPL_H
#define __AUDIOCD_STREAM_INFO_IMPL_H

#include <glib-object.h>

#include "monkey-media-stream-info.h"

G_BEGIN_DECLS

#define TYPE_AUDIOCD_STREAM_INFO_IMPL         (audiocd_stream_info_impl_get_type ())
#define AUDIOCD_STREAM_INFO_IMPL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), TYPE_AUDIOCD_STREAM_INFO_IMPL, AudioCDStreamInfoImpl))
#define AUDIOCD_STREAM_INFO_IMPL_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), TYPE_AUDIOCD_STREAM_INFO_IMPL, AudioCDStreamInfoImplClass))
#define IS_AUDIOCD_STREAM_INFO_IMPL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), TYPE_AUDIOCD_STREAM_INFO_IMPL))
#define IS_AUDIOCD_STREAM_INFO_IMPL_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), TYPE_AUDIOCD_STREAM_INFO_IMPL))
#define AUDIOCD_STREAM_INFO_IMPL_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), TYPE_AUDIOCD_STREAM_INFO_IMPL, AudioCDStreamInfoImplClass))

typedef struct AudioCDStreamInfoImplPrivate AudioCDStreamInfoImplPrivate;

typedef struct
{
	MonkeyMediaStreamInfo parent;

	AudioCDStreamInfoImplPrivate *priv;
} AudioCDStreamInfoImpl;

typedef struct
{
	MonkeyMediaStreamInfoClass parent_class;
} AudioCDStreamInfoImplClass;

GType audiocd_stream_info_impl_get_type (void);

G_END_DECLS

#endif /* __AUDIOCD_STREAM_INFO_IMPL_H */
