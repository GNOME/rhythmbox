/* 
 *  Copyright (C) 2002 Colin Walters <walters@gnu.org>
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
 *  $Id$
 */

#include <config.h>

#include "rb-iradio-station.h"
#include "rb-debug.h"
#include <limits.h>
#include <monkey-media-audio-stream.h>

static void rb_iradio_station_class_init (RBIRadioStationClass *klass);
static void rb_iradio_station_init (RBIRadioStation *view);
static void rb_iradio_station_finalize (GObject *object);
static void rb_iradio_station_set_property (GObject *object,
					     guint prop_id,
					     const GValue *value,
					     GParamSpec *pspec);
static void rb_iradio_station_get_property (GObject *object,
					     guint prop_id,
					     GValue *value,
					     GParamSpec *pspec);

enum
{
	PROP_0,
	PROP_LOCATIONS,
	PROP_GENRE,
	PROP_NAME,
};

struct RBIRadioStationPrivate
{
	const char *genre;
	const char *name;
	GList *locations;
	MonkeyMediaAudioStream *stream;
};

static GObjectClass *parent_class = NULL;

GType
rb_iradio_station_get_type (void)
{
	static GType rb_iradio_station_type = 0;

	if (rb_iradio_station_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBIRadioStationClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_iradio_station_class_init,
			NULL,
			NULL,
			sizeof (RBIRadioStation),
			0,
			(GInstanceInitFunc) rb_iradio_station_init
		};

		rb_iradio_station_type = g_type_register_static (G_TYPE_OBJECT,
								 "RBIRadioStation",
								 &our_info, 0);
		
	}

	return rb_iradio_station_type;
}

static void
rb_iradio_station_class_init (RBIRadioStationClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	parent_class = g_type_class_peek_parent (klass);
	
	object_class->finalize = rb_iradio_station_finalize;
	object_class->set_property = rb_iradio_station_set_property;
	object_class->get_property = rb_iradio_station_get_property;
	
	g_object_class_install_property (object_class,
					 PROP_LOCATIONS,
					 g_param_spec_pointer ("locations",
							       "Locations",
							       "Locations",
							       G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_GENRE,
					 g_param_spec_string ("genre",
							      "Genre",
							      "Genre",
							      "",
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_NAME,
					 g_param_spec_string ("name",
							      "Name",
							      "Station Name",
							      "",
							      G_PARAM_READWRITE));
}

static void
rb_iradio_station_init (RBIRadioStation *station)
{
	station->priv = g_new0(RBIRadioStationPrivate, 1);
}

static void
rb_iradio_station_finalize (GObject *object)
{
	RBIRadioStation *station;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_IRADIO_STATION (object));

	station = RB_IRADIO_STATION (object);

	g_return_if_fail (station->priv != NULL);

	g_free (station->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_iradio_station_set_property (GObject *object,
				guint prop_id,
				const GValue *value,
				GParamSpec *pspec)
{
	RBIRadioStation *station = RB_IRADIO_STATION (object);

	switch (prop_id)
	{
	case PROP_LOCATIONS:
	{
		station->priv->locations = (GList *) g_value_get_pointer (value);
		break;
	}
	case PROP_GENRE:
	{
		station->priv->genre = g_strdup(g_value_get_string (value));
		break;
	}
	case PROP_NAME:
	{
		station->priv->name = g_strdup(g_value_get_string (value));
		break;
	}
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_iradio_station_get_property (GObject *object,
				 guint prop_id,
				 GValue *value,
				 GParamSpec *pspec)
{
	RBIRadioStation *station = RB_IRADIO_STATION (object);

	switch (prop_id)
	{
	case PROP_LOCATIONS:
	{
		g_value_set_pointer(value, station->priv->locations);
		break;
	}
	case PROP_GENRE:
	{
		g_value_set_string(value, station->priv->genre);
		break;
	}
	case PROP_NAME:
	{
		g_value_set_string(value, station->priv->name);
		break;
	}
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}
