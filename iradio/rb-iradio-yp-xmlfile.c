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

#include "rb-iradio-yp-xmlfile.h"
#include "rb-iradio-station.h"
#include "rb-file-helpers.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#include <libgnome/gnome-i18n.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

static void rb_iradio_yp_xmlfile_class_init (RBIRadioYPXMLFileClass *klass);
static void rb_iradio_yp_xmlfile_init (RBIRadioYPXMLFile *view);
static void rb_iradio_yp_xmlfile_finalize (GObject *object);
static void rb_iradio_yp_xmlfile_set_property (GObject *object,
					       guint prop_id,
					       const GValue *value,
					       GParamSpec *pspec);
static void rb_iradio_yp_xmlfile_get_property (GObject *object,
					       guint prop_id,
					       GValue *value,
					       GParamSpec *pspec);
static void rb_iradio_yp_xmlfile_iterator_init (RBIRadioYPIteratorIface *iface);

RBIRadioStation * rb_iradio_yp_xmlfile_impl_get_next_station(RBIRadioYPIterator *it);
static void parse_ctx_destroy_notify(gpointer data);
static void rb_iradio_yp_xmlfile_start_element_handler (GMarkupParseContext *context,
							const gchar         *element_name,
							const gchar        **attribute_names,
							const gchar        **attribute_values,
							gpointer             user_data,
							GError             **error);

static void rb_iradio_yp_xmlfile_end_element_handler (GMarkupParseContext *context,
						      const gchar         *element_name,
						      gpointer             user_data,
						      GError             **error);
static void rb_iradio_yp_xmlfile_text_handler (GMarkupParseContext *context,
					       const gchar         *text,
					       gsize                text_len,
					       gpointer             user_data,
					       GError             **error);
enum
{
	PROP_0,
	PROP_FILENAME,
};

typedef enum
{
	RB_IRADIO_YP_XMLFILE_STATE_START,
	RB_IRADIO_YP_XMLFILE_STATE_IN_IRADIO_CACHE,
	RB_IRADIO_YP_XMLFILE_STATE_IN_CACHE_NAME,
	RB_IRADIO_YP_XMLFILE_STATE_IN_STATION,
	RB_IRADIO_YP_XMLFILE_STATE_IN_GENRE,
	RB_IRADIO_YP_XMLFILE_STATE_IN_NAME,
	RB_IRADIO_YP_XMLFILE_STATE_IN_URL,
	RB_IRADIO_YP_XMLFILE_STATE_END,
} RBIRadioYPXMLFileParserState;

typedef struct 
{
	GString *cache_name;
	GString *name;
	GString *tmp_url;
	GList *locations;
	GString *genre;
	RBIRadioYPXMLFileParserState state;

	GList *result;
} RBIRadioYPXMLFileParserStateData;

struct RBIRadioYPXMLFilePrivate
{
	const char *filename;
	FILE *parsingfile;
	GMarkupParseContext *parsectx;
	RBIRadioYPXMLFileParserStateData *parserdata;
};

static GObjectClass *parent_class = NULL;

GType
rb_iradio_yp_xmlfile_get_type (void)
{
	static GType rb_iradio_yp_xmlfile_type = 0;

	if (rb_iradio_yp_xmlfile_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBIRadioYPXMLFileClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_iradio_yp_xmlfile_class_init,
			NULL,
			NULL,
			sizeof (RBIRadioYPXMLFile),
			0,
			(GInstanceInitFunc) rb_iradio_yp_xmlfile_init
		};

		static const GInterfaceInfo iterator_info =
		{
			(GInterfaceInitFunc) rb_iradio_yp_xmlfile_iterator_init,
			NULL,
			NULL
		};
		
		rb_iradio_yp_xmlfile_type = g_type_register_static (G_TYPE_OBJECT,
								    "RBIRadioYPXMLFile",
								    &our_info, 0);
		
		g_type_add_interface_static (rb_iradio_yp_xmlfile_type,
					     RB_TYPE_IRADIO_YP_ITERATOR,
					     &iterator_info);
	}

	return rb_iradio_yp_xmlfile_type;
}

static void
rb_iradio_yp_xmlfile_class_init (RBIRadioYPXMLFileClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_iradio_yp_xmlfile_finalize;
	object_class->set_property = rb_iradio_yp_xmlfile_set_property;
	object_class->get_property = rb_iradio_yp_xmlfile_get_property;

	g_object_class_install_property (object_class,
					 PROP_FILENAME,
					 g_param_spec_string ("filename",
							      "Filename",
							      "Source filename",
							      "",
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
rb_iradio_yp_xmlfile_init (RBIRadioYPXMLFile *ypxml)
{
	ypxml->priv = g_new(RBIRadioYPXMLFilePrivate, 1);
	ypxml->priv->filename = NULL;
	fprintf(stderr, "ypxmlfile: created %p\n", ypxml);
}

static void
rb_iradio_yp_xmlfile_finalize (GObject *object)
{
	RBIRadioYPXMLFile *ypxml;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_IRADIO_YP_XMLFILE (object));

	ypxml = RB_IRADIO_YP_XMLFILE (object);

	g_return_if_fail (ypxml->priv != NULL);
	fprintf(stderr, "ypxmlfile: finalized\n");

	g_free (ypxml->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_iradio_yp_xmlfile_iterator_init (RBIRadioYPIteratorIface *iface)
{
	iface->impl_get_next_station = rb_iradio_yp_xmlfile_impl_get_next_station;
}

static void
rb_iradio_yp_xmlfile_set_property (GObject *object,
				 guint prop_id,
				 const GValue *value,
				 GParamSpec *pspec)
{
	RBIRadioYPXMLFile *ypxml = RB_IRADIO_YP_XMLFILE (object);

	switch (prop_id)
	{
	case PROP_FILENAME:
	{
		static const GMarkupParser parser_impl = {rb_iradio_yp_xmlfile_start_element_handler,
							  rb_iradio_yp_xmlfile_end_element_handler,
							  rb_iradio_yp_xmlfile_text_handler,
							  NULL,
							  /* FIXME add error handler */
							  NULL};
		ypxml->priv->filename = g_value_dup_string (value);
		ypxml->priv->parserdata = g_new0(RBIRadioYPXMLFileParserStateData, 1);
		ypxml->priv->parserdata->result = NULL;
		ypxml->priv->parserdata->state = RB_IRADIO_YP_XMLFILE_STATE_START;
		ypxml->priv->parsectx = g_markup_parse_context_new(&parser_impl, 0,
								   ypxml->priv->parserdata,
								   parse_ctx_destroy_notify);
		if (!(ypxml->priv->parsingfile = fopen(ypxml->priv->filename, "r")))
		{
			fprintf(stderr, _("Failed to open %s: %s"), ypxml->priv->filename, g_strerror(errno));
		}
		break;
	}
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_iradio_yp_xmlfile_get_property (GObject *object,
				 guint prop_id,
				 GValue *value,
				 GParamSpec *pspec)
{
	RBIRadioYPXMLFile *ypxml = RB_IRADIO_YP_XMLFILE (object);

	switch (prop_id)
	{
	case PROP_FILENAME:
		g_value_set_string (value, ypxml->priv->filename);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void rb_iradio_yp_xmlfile_start_element_handler (GMarkupParseContext *context,
							const gchar         *element_name,
							const gchar        **attribute_names,
							const gchar        **attribute_values,
							gpointer             user_data,
							GError             **error)
{
	RBIRadioYPXMLFileParserStateData *statedata = (RBIRadioYPXMLFileParserStateData *) user_data;

	switch (statedata->state)
	{
	case RB_IRADIO_YP_XMLFILE_STATE_START:
	{
		if (strncmp(element_name, "iradio-cache", 12))
			goto invalid_element;
		statedata->state = RB_IRADIO_YP_XMLFILE_STATE_IN_IRADIO_CACHE;
		break;
	}
	case RB_IRADIO_YP_XMLFILE_STATE_IN_IRADIO_CACHE:
	{
		if (!strncmp(element_name, "name", 4))
		{
			statedata->cache_name = g_string_new("");
			statedata->state = RB_IRADIO_YP_XMLFILE_STATE_IN_CACHE_NAME;
		}
		else if (!strcmp(element_name, "station"))
		{
			statedata->name = g_string_new("");
			statedata->locations = NULL;
			statedata->tmp_url = g_string_new("");
			statedata->genre = g_string_new("");
			statedata->state = RB_IRADIO_YP_XMLFILE_STATE_IN_STATION;
		}
		break;
	}
	case RB_IRADIO_YP_XMLFILE_STATE_IN_STATION:
	{
		if (!strncmp(element_name, "name", 4))
			statedata->state = RB_IRADIO_YP_XMLFILE_STATE_IN_NAME;
		else if (!strncmp(element_name, "url", 3))
			statedata->state = RB_IRADIO_YP_XMLFILE_STATE_IN_URL;
		else if (!strncmp(element_name, "genre", 5))
			statedata->state = RB_IRADIO_YP_XMLFILE_STATE_IN_GENRE;
		break;
	}
	default:
		goto invalid_element;
	}
	return;

invalid_element:
	g_set_error (error,
		     G_MARKUP_ERROR,
		     G_MARKUP_ERROR_INVALID_CONTENT,
		     "Invalid start element %s",
		     element_name);
	return;
}


static void rb_iradio_yp_xmlfile_end_element_handler (GMarkupParseContext *context,
						      const gchar         *element_name,
						      gpointer             user_data,
						      GError             **error)
{
	RBIRadioYPXMLFileParserStateData *statedata = (RBIRadioYPXMLFileParserStateData *) user_data;

	switch (statedata->state)
	{
	case RB_IRADIO_YP_XMLFILE_STATE_IN_IRADIO_CACHE:
	{
		statedata->state = RB_IRADIO_YP_XMLFILE_STATE_END;
		break;
	}
	case RB_IRADIO_YP_XMLFILE_STATE_IN_CACHE_NAME:
	{
		statedata->state = RB_IRADIO_YP_XMLFILE_STATE_IN_IRADIO_CACHE;
		break;
	}
	case RB_IRADIO_YP_XMLFILE_STATE_IN_STATION:
	{
		RBIRadioStation *ret;
		char *genre;
		char *name;

		if (!(statedata->genre && statedata->name && statedata->locations))
		{
			fprintf (stderr, "Incomplete station, skipping.\n");
			goto station_incomplete;
		}
		genre = g_string_free(statedata->genre, FALSE);
		name = g_string_free(statedata->name, FALSE);

		ret = g_object_new(RB_TYPE_IRADIO_STATION,
				   "genre", genre,
				   "name", name,
				   "locations", statedata->locations,
				   NULL);
		g_free(genre);
		g_free(name);
		statedata->result = g_list_append(statedata->result, ret);
	station_incomplete:
		statedata->state = RB_IRADIO_YP_XMLFILE_STATE_IN_IRADIO_CACHE;
		break;
	}
	case RB_IRADIO_YP_XMLFILE_STATE_IN_GENRE:
	case RB_IRADIO_YP_XMLFILE_STATE_IN_NAME:
	{
		statedata->state = RB_IRADIO_YP_XMLFILE_STATE_IN_STATION;
		break;
	}
	case RB_IRADIO_YP_XMLFILE_STATE_IN_URL:
	{
		statedata->locations = g_list_append (statedata->locations,
						      g_string_free (statedata->tmp_url, FALSE));
		statedata->state = RB_IRADIO_YP_XMLFILE_STATE_IN_STATION;
		break;
	}
	default:
		goto invalid_element;
	}
	return;

invalid_element:
	g_set_error (error,
		     G_MARKUP_ERROR,
		     G_MARKUP_ERROR_INVALID_CONTENT,
		     "Invalid end element %s",
		     element_name);
	return;
}

static void
rb_iradio_yp_xmlfile_text_handler (GMarkupParseContext *context,
				   const gchar         *text,
				   gsize                text_len,
				   gpointer             user_data,
				   GError             **error)
{
	RBIRadioYPXMLFileParserStateData *statedata = (RBIRadioYPXMLFileParserStateData *) user_data;
	GString *val;

	switch (statedata->state)
	{
	case RB_IRADIO_YP_XMLFILE_STATE_IN_CACHE_NAME:
	{
		val = statedata->cache_name;
		break;
	}
	case RB_IRADIO_YP_XMLFILE_STATE_IN_GENRE:
	{
		val = statedata->genre;
		break;
	}
	case RB_IRADIO_YP_XMLFILE_STATE_IN_NAME:
	{
		val = statedata->name;
		break;
	}
	case RB_IRADIO_YP_XMLFILE_STATE_IN_URL:
	{
		val = statedata->tmp_url;
		break;
	}
	default:
/* 		goto invalid_element; */
		return;
	}

	g_string_append_len(val, text, text_len);
	return;

/* invalid_element: */
/* 	g_set_error (error, */
/* 		     G_MARKUP_ERROR, */
/* 		     G_MARKUP_ERROR_INVALID_CONTENT, */
/* 		     "Invalid text \"%s\"", text); */
/* 	return; */
}

static void parse_ctx_destroy_notify(gpointer data)
{
	fprintf(stderr, "destroying parsing context\n");
	g_free((RBIRadioYPXMLFileParserStateData *) data);
}

RBIRadioStation *
rb_iradio_yp_xmlfile_impl_get_next_station(RBIRadioYPIterator *it)
{
	RBIRadioStation *ret = NULL;
	RBIRadioYPXMLFile *ypxml = RB_IRADIO_YP_XMLFILE(it);

	while (ypxml->priv->parserdata->result == NULL
	       && ypxml->priv->parsingfile != NULL
	       && !feof(ypxml->priv->parsingfile))
	{
		GError *err = NULL;
		char buf[1024];
		ssize_t len;
		if ((len = fread(buf, 1, sizeof(buf), ypxml->priv->parsingfile)) < 0)
		{
			fprintf(stderr, _("Failed to read %s: %s"), ypxml->priv->filename, g_strerror(errno));
			goto lose;
		}
		g_markup_parse_context_parse(ypxml->priv->parsectx, buf, len, &err);
		if (err != NULL)
		{
			fprintf(stderr, _("Failed to parse %s: %s"), ypxml->priv->filename, err->message);
			goto lose;
		}
	}

	if (ypxml->priv->parsingfile != NULL && feof(ypxml->priv->parsingfile))
	{
		/* FIXME error handling */
		g_markup_parse_context_end_parse(ypxml->priv->parsectx, NULL);
	}

	if (ypxml->priv->parserdata->result == NULL)
	{
		ret = NULL;
		g_markup_parse_context_free(ypxml->priv->parsectx);
		fclose(ypxml->priv->parsingfile);
		ypxml->priv->parsingfile = NULL;
	}
	else
	{
		GList *first = g_list_first(ypxml->priv->parserdata->result);
		ypxml->priv->parserdata->result = g_list_remove_link(ypxml->priv->parserdata->result,
								     first);
		ret = RB_IRADIO_STATION(first->data);
		g_list_free_1(first);
		
	}
	g_assert (ret == NULL || RB_IS_IRADIO_STATION (ret));
	return ret;
lose:
	g_list_free(ypxml->priv->parserdata->result);
	ypxml->priv->parserdata->result = NULL;
	return NULL;
}
