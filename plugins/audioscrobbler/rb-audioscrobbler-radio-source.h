/*
 * rb-audioscrobbler-radio-source.h
 *
 * Copyright (C) 2010 Jamie Nicol <jamie@thenicols.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * The Rhythmbox authors hereby grant permission for non-GPL compatible
 * GStreamer plugins to be used and distributed together with GStreamer
 * and Rhythmbox. This permission is above and beyond the permissions granted
 * by the GPL license by which Rhythmbox is covered. If you modify this code
 * you may extend this exception to your version of the code, but you are not
 * obligated to do so. If you do not wish to do so, delete this exception
 * statement from your version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 */

#ifndef __RB_AUDIOSCROBBLER_RADIO_SOURCE_H
#define __RB_AUDIOSCROBBLER_RADIO_SOURCE_H

#include "rb-streaming-source.h"
#include "rb-audioscrobbler-profile-page.h"
#include "rb-audioscrobbler-service.h"
#include "rb-audioscrobbler-account.h"

G_BEGIN_DECLS

typedef enum
{
	RB_AUDIOSCROBBLER_RADIO_TYPE_SIMILAR_ARTISTS,
	RB_AUDIOSCROBBLER_RADIO_TYPE_TOP_FANS,
	RB_AUDIOSCROBBLER_RADIO_TYPE_LIBRARY,
	RB_AUDIOSCROBBLER_RADIO_TYPE_NEIGHBOURS,
	RB_AUDIOSCROBBLER_RADIO_TYPE_LOVED_TRACKS,
	RB_AUDIOSCROBBLER_RADIO_TYPE_RECOMMENDATION,
	RB_AUDIOSCROBBLER_RADIO_TYPE_MIX,
	RB_AUDIOSCROBBLER_RADIO_TYPE_GLOBAL_TAG,
	RB_AUDIOSCROBBLER_RADIO_TYPE_GROUP,

	RB_AUDIOSCROBBLER_RADIO_TYPE_LAST
} RBAudioscrobblerRadioType;

const char *rb_audioscrobbler_radio_type_get_text (RBAudioscrobblerRadioType type);
const char *rb_audioscrobbler_radio_type_get_url (RBAudioscrobblerRadioType type);
const char *rb_audioscrobbler_radio_type_get_default_name (RBAudioscrobblerRadioType type);

#define RB_TYPE_AUDIOSCROBBLER_RADIO_SOURCE         (rb_audioscrobbler_radio_source_get_type ())
#define RB_AUDIOSCROBBLER_RADIO_SOURCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_AUDIOSCROBBLER_RADIO_SOURCE, RBAudioscrobblerRadioSource))
#define RB_AUDIOSCROBBLER_RADIO_SOURCE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), RB_TYPE_AUDIOSCROBBLER_RADIO_SOURCE, RBAudioscrobblerRadioSourceClass))
#define RB_IS_AUDIOSCROBBLER_RADIO_SOURCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_AUDIOSCROBBLER_RADIO_SOURCE))
#define RB_IS_AUDIOSCROBBLER_RADIO_SOURCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_AUDIOSCROBBLER_RADIO_SOURCE))
#define RB_AUDIOSCROBBLER_RADIO_SOURCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_AUDIOSCROBBLER_RADIO_SOURCE, RBAudioscrobblerRadioSourceClass))

typedef struct _RBAudioscrobblerRadioSourcePrivate RBAudioscrobblerRadioSourcePrivate;

typedef struct
{
	RBStreamingSource parent;

	RBAudioscrobblerRadioSourcePrivate *priv;
} RBAudioscrobblerRadioSource;

typedef struct
{
	RBStreamingSourceClass parent_class;
} RBAudioscrobblerRadioSourceClass;

GType rb_audioscrobbler_radio_source_get_type (void);
void _rb_audioscrobbler_radio_source_register_type (GTypeModule *module);

RBSource *rb_audioscrobbler_radio_source_new (RBAudioscrobblerProfilePage *parent,
                                              RBAudioscrobblerService *service,
                                              const char *username,
                                              const char *session_key,
                                              const char *station_name,
                                              const char *station_url);

G_END_DECLS

#endif /* __RB_AUDIOSCROBBLER_RADIO_SOURCE_H */
