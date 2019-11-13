/*
 * rb-audioscrobbler-service.c
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

#include "config.h"

#include "rb-audioscrobbler-service.h"

/* Last.fm details */
#define LASTFM_NAME "Last.fm"
#define LASTFM_AUTH_URL "https://www.last.fm/api/auth/"
#define LASTFM_SCROBBLER_URL "http://post.audioscrobbler.com/"
#define LASTFM_API_URL "https://ws.audioscrobbler.com/2.0/"
#define LASTFM_OLD_RADIO_API_URL "https://ws.audioscrobbler.com/"
/* this API key belongs to Jamie Nicol <jamie@thenicols.net>
   generated May 2010 for use in the audioscrobbler plugin */
#define LASTFM_API_KEY "0337ff3c59299b6a31d75164041860b6"
#define LASTFM_API_SECRET "776c85a04a445efa8f9ed7705473c606"

#define LIBREFM_NAME "Libre.fm"
#define LIBREFM_AUTH_URL "http://alpha.libre.fm/api/auth/"
#define LIBREFM_SCROBBLER_URL "http://turtle.libre.fm/"
#define LIBREFM_API_URL "http://alpha.libre.fm/2.0/"
#define LIBREFM_API_KEY "a string 32 characters in length"
#define LIBREFM_API_SECRET "a string 32 characters in length"

struct _RBAudioscrobblerServicePrivate {
	char *name;
	char *auth_url;
	char *scrobbler_url;
	char *api_url;
	char *old_radio_api_url;
	char *api_key;
	char *api_secret;
};

#define RB_AUDIOSCROBBLER_SERVICE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_AUDIOSCROBBLER_SERVICE, RBAudioscrobblerServicePrivate))

static void rb_audioscrobbler_service_finalize (GObject *object);
static void rb_audioscrobbler_service_get_property (GObject *object,
                                                    guint prop_id,
                                                    GValue *value,
                                                    GParamSpec *pspec);
static void rb_audioscrobbler_service_set_property (GObject *object,
                                                    guint prop_id,
                                                    const GValue *value,
                                                    GParamSpec *pspec);

enum
{
	PROP_0,
	PROP_NAME,
	PROP_AUTH_URL,
	PROP_SCROBBLER_URL,
	PROP_API_URL,
	PROP_OLD_RADIO_API_URL,
	PROP_API_KEY,
	PROP_API_SECRET,
};

G_DEFINE_DYNAMIC_TYPE (RBAudioscrobblerService, rb_audioscrobbler_service, G_TYPE_OBJECT)

RBAudioscrobblerService *
rb_audioscrobbler_service_new_lastfm (void)
{
	/* create a Last.fm service */
	return g_object_new (RB_TYPE_AUDIOSCROBBLER_SERVICE,
	                     "name", LASTFM_NAME,
	                     "auth-url", LASTFM_AUTH_URL,
	                     "scrobbler-url", LASTFM_SCROBBLER_URL,
	                     "api-url", LASTFM_API_URL,
	                     "old-radio-api-url", LASTFM_OLD_RADIO_API_URL,
	                     "api-key", LASTFM_API_KEY,
	                     "api-secret", LASTFM_API_SECRET,
	                     NULL);
}

RBAudioscrobblerService *
rb_audioscrobbler_service_new_librefm (void)
{
	/* create a Libre.fm service */
	return g_object_new (RB_TYPE_AUDIOSCROBBLER_SERVICE,
	                     "name", LIBREFM_NAME,
	                     "auth-url", LIBREFM_AUTH_URL,
	                     "scrobbler-url", LIBREFM_SCROBBLER_URL,
	                     "api-url", LIBREFM_API_URL,
	                     "api-key", LIBREFM_API_KEY,
	                     "api-secret", LIBREFM_API_SECRET,
	                     NULL);
}

static void
rb_audioscrobbler_service_class_init (RBAudioscrobblerServiceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = rb_audioscrobbler_service_finalize;
	object_class->get_property = rb_audioscrobbler_service_get_property;
	object_class->set_property = rb_audioscrobbler_service_set_property;

	g_object_class_install_property (object_class,
	                                 PROP_NAME,
	                                 g_param_spec_string ("name",
	                                                      "Name",
	                                                      "Name of the service",
	                                                      NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
	                                 PROP_AUTH_URL,
	                                 g_param_spec_string ("auth-url",
	                                                      "Authentication URL",
	                                                      "URL user should be taken to for authentication",
	                                                      NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
	                                 PROP_SCROBBLER_URL,
	                                 g_param_spec_string ("scrobbler-url",
	                                                      "Scrobbler URL",
	                                                      "URL scrobbler sessions should be made with",
	                                                      NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
	                                 PROP_API_URL,
	                                 g_param_spec_string ("api-url",
	                                                      "API URL",
	                                                      "URL API requests should be sent to",
	                                                      NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
	                                 PROP_OLD_RADIO_API_URL,
	                                 g_param_spec_string ("old-radio-api-url",
	                                                      "Old Radio API URL",
	                                                      "URL that radio requests using the old API should be sent to",
	                                                      NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
	                                 PROP_API_KEY,
	                                 g_param_spec_string ("api-key",
	                                                      "API Key",
	                                                      "API key",
	                                                      NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
	                                 PROP_API_SECRET,
	                                 g_param_spec_string ("api-secret",
	                                                      "API Secret",
	                                                      "API secret",
	                                                      NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (RBAudioscrobblerServicePrivate));
}

static void
rb_audioscrobbler_service_class_finalize (RBAudioscrobblerServiceClass *klass)
{
}

static void
rb_audioscrobbler_service_init (RBAudioscrobblerService *service)
{
	service->priv = RB_AUDIOSCROBBLER_SERVICE_GET_PRIVATE (service);
}

static void
rb_audioscrobbler_service_finalize (GObject *object)
{
	RBAudioscrobblerService *service = RB_AUDIOSCROBBLER_SERVICE (object);

	g_free (service->priv->name);
	g_free (service->priv->auth_url);
	g_free (service->priv->scrobbler_url);
	g_free (service->priv->api_url);
	g_free (service->priv->old_radio_api_url);
	g_free (service->priv->api_key);
	g_free (service->priv->api_secret);

	G_OBJECT_CLASS (rb_audioscrobbler_service_parent_class)->finalize (object);
}

static void
rb_audioscrobbler_service_get_property (GObject *object,
                                        guint prop_id,
                                        GValue *value,
                                        GParamSpec *pspec)
{
	RBAudioscrobblerService *service = RB_AUDIOSCROBBLER_SERVICE (object);

	switch (prop_id) {
	case PROP_NAME:
		g_value_set_string (value, rb_audioscrobbler_service_get_name (service));
		break;
	case PROP_AUTH_URL:
		g_value_set_string (value, rb_audioscrobbler_service_get_auth_url (service));
		break;
	case PROP_SCROBBLER_URL:
		g_value_set_string (value, rb_audioscrobbler_service_get_scrobbler_url (service));
		break;
	case PROP_API_URL:
		g_value_set_string (value, rb_audioscrobbler_service_get_api_url (service));
		break;
	case PROP_OLD_RADIO_API_URL:
		g_value_set_string (value, rb_audioscrobbler_service_get_old_radio_api_url (service));
		break;
	case PROP_API_KEY:
		g_value_set_string (value, rb_audioscrobbler_service_get_api_key (service));
		break;
	case PROP_API_SECRET:
		g_value_set_string (value, rb_audioscrobbler_service_get_api_secret (service));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_audioscrobbler_service_set_property (GObject *object,
                                        guint prop_id,
                                        const GValue *value,
                                        GParamSpec *pspec)
{
	RBAudioscrobblerService *service = RB_AUDIOSCROBBLER_SERVICE (object);

	switch (prop_id) {
	case PROP_NAME:
		g_free (service->priv->name);
		service->priv->name = g_value_dup_string (value);
		break;
	case PROP_AUTH_URL:
		g_free (service->priv->auth_url);
		service->priv->auth_url = g_value_dup_string (value);
		break;
	case PROP_SCROBBLER_URL:
		g_free (service->priv->scrobbler_url);
		service->priv->scrobbler_url = g_value_dup_string (value);
		break;
	case PROP_API_URL:
		g_free (service->priv->api_url);
		service->priv->api_url = g_value_dup_string (value);
		break;
	case PROP_OLD_RADIO_API_URL:
		g_free (service->priv->old_radio_api_url);
		service->priv->old_radio_api_url = g_value_dup_string (value);
		break;
	case PROP_API_KEY:
		g_free (service->priv->api_key);
		service->priv->api_key = g_value_dup_string (value);
		break;
	case PROP_API_SECRET:
		g_free (service->priv->api_secret);
		service->priv->api_secret = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

const char *
rb_audioscrobbler_service_get_name (RBAudioscrobblerService *service)
{
	return service->priv->name;
}

const char *
rb_audioscrobbler_service_get_auth_url (RBAudioscrobblerService *service)
{
	return service->priv->auth_url;
}

const char *
rb_audioscrobbler_service_get_scrobbler_url (RBAudioscrobblerService *service)
{
	return service->priv->scrobbler_url;
}

const char *
rb_audioscrobbler_service_get_api_url (RBAudioscrobblerService *service)
{
	return service->priv->api_url;
}

const char *
rb_audioscrobbler_service_get_old_radio_api_url (RBAudioscrobblerService *service)
{
	return service->priv->old_radio_api_url;
}

const char *
rb_audioscrobbler_service_get_api_key (RBAudioscrobblerService *service)
{
	return service->priv->api_key;
}

const char *
rb_audioscrobbler_service_get_api_secret (RBAudioscrobblerService *service)
{
	return service->priv->api_secret;
}

void
_rb_audioscrobbler_service_register_type (GTypeModule *module)
{
	rb_audioscrobbler_service_register_type (module);
}
