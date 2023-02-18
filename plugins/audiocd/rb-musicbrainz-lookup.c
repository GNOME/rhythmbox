/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Jonathan Matthew
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

#include <stdlib.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <libsoup/soup.h>

#include "rb-musicbrainz-lookup.h"


struct ParseAttrMap {
	const char *path;
	const char *xml_attr;
	const char *attr;
};

struct _RBMusicBrainzData {
	char *type;
	GHashTable *attrs;
	GList *children;
	RBMusicBrainzData *parent;

	GList *path_start;
};

typedef struct {
	RBMusicBrainzData *current;
	RBMusicBrainzData *root;

	GQueue path;
	const char *item;
	GString text;

	struct ParseAttrMap *map;
} RBMusicBrainzParseContext;


static struct ParseAttrMap root_attr_map[] = {
	{ NULL, NULL, NULL }
};

static struct ParseAttrMap release_attr_map[] = {
	{ "/release", "id", RB_MUSICBRAINZ_ATTR_ALBUM_ID },
	{ "/release/asin", NULL, RB_MUSICBRAINZ_ATTR_ASIN },
	{ "/release/country", NULL, RB_MUSICBRAINZ_ATTR_COUNTRY },
	{ "/release/date", NULL, RB_MUSICBRAINZ_ATTR_DATE },
	{ "/release/title", NULL, RB_MUSICBRAINZ_ATTR_ALBUM },
	{ "/release/artist-credit/name-credit/artist", "id", RB_MUSICBRAINZ_ATTR_ALBUM_ARTIST_ID },
	{ "/release/artist-credit/name-credit/artist/name", NULL, RB_MUSICBRAINZ_ATTR_ALBUM_ARTIST },
	{ "/release/artist-credit/name-credit/artist/sort-name", NULL, RB_MUSICBRAINZ_ATTR_ALBUM_ARTIST_SORTNAME },
	{ NULL, NULL, NULL }
};

static struct ParseAttrMap medium_attr_map[] = {
	{ "/medium/position", NULL, RB_MUSICBRAINZ_ATTR_DISC_NUMBER },
	{ "/medium/track-list", "count", RB_MUSICBRAINZ_ATTR_TRACK_COUNT },
	{ "/medium/disc-list/disc", "id", RB_MUSICBRAINZ_ATTR_DISC_ID },
	{ NULL, NULL, NULL }
};

static struct ParseAttrMap track_attr_map[] = {
	{ "/track/number", NULL, RB_MUSICBRAINZ_ATTR_TRACK_NUMBER },
	{ "/track/length", NULL, RB_MUSICBRAINZ_ATTR_DURATION },
	{ "/track/recording", "id", RB_MUSICBRAINZ_ATTR_TRACK_ID },
	{ "/track/recording/title", NULL, RB_MUSICBRAINZ_ATTR_TITLE },
	{ "/track/recording/artist-credit/name-credit/artist", "id", RB_MUSICBRAINZ_ATTR_ARTIST_ID },
	{ "/track/recording/artist-credit/name-credit/artist/name", NULL, RB_MUSICBRAINZ_ATTR_ARTIST },
	{ "/track/recording/artist-credit/name-credit/artist/sort-name", NULL, RB_MUSICBRAINZ_ATTR_ARTIST_SORTNAME },
	{ NULL, NULL, NULL }
};

static struct ParseAttrMap relation_attr_map[] = {
	{ "/relation", "type", RB_MUSICBRAINZ_ATTR_RELATION_TYPE },
	{ "/relation/target", NULL, RB_MUSICBRAINZ_ATTR_RELATION_TARGET },
	{ "/relation/artist", "id", RB_MUSICBRAINZ_ATTR_ARTIST_ID },
	{ "/relation/artist/name", NULL, RB_MUSICBRAINZ_ATTR_ARTIST },
	{ "/relation/artist/sortname", NULL, RB_MUSICBRAINZ_ATTR_ARTIST_SORTNAME },
	{ "/relation/work", "id", RB_MUSICBRAINZ_ATTR_WORK_ID },
	{ "/relation/work/title", NULL, RB_MUSICBRAINZ_ATTR_WORK_TITLE },
	{ NULL, NULL, NULL }
};

static struct {
	const char *name;
	struct ParseAttrMap *map;
} object_types[] = {
	{ "root", root_attr_map },
	{ "release", release_attr_map },
	{ "medium", medium_attr_map },
	{ "track", track_attr_map },
	{ "relation", relation_attr_map }
};

GQuark
rb_musicbrainz_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("rb_musicbrainz_error");

	return quark;
}

#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }
GType
rb_musicbrainz_error_get_type (void)
{
	static GType etype = 0;

	if (etype == 0) {
		static const GEnumValue values[] = {
			ENUM_ENTRY (RB_MUSICBRAINZ_ERROR_NOT_FOUND, "not-found"),
			ENUM_ENTRY (RB_MUSICBRAINZ_ERROR_SERVER, "server-error"),
			ENUM_ENTRY (RB_MUSICBRAINZ_ERROR_NETWORK, "network-error"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RBMusicBrainzError", values);
	}

	return etype;
}


static void
free_values (GQueue *attrs)
{
	g_queue_free_full (attrs, (GDestroyNotify) g_free);
}

void
rb_musicbrainz_data_free (RBMusicBrainzData *data)
{
	g_hash_table_unref (data->attrs);
	g_list_free_full (data->children, (GDestroyNotify) rb_musicbrainz_data_free);
	g_free (data->type);
	g_free (data);
}
const char *
rb_musicbrainz_data_get_attr_value (RBMusicBrainzData *data, const char *attr)
{
	GQueue *d;
	d = g_hash_table_lookup (data->attrs, attr);
	if (d == NULL) {
		return NULL;
	}

	return d->head->data;
}

GList *
rb_musicbrainz_data_get_attr_names (RBMusicBrainzData *data)
{
	return g_hash_table_get_keys (data->attrs);
}

RBMusicBrainzData *
rb_musicbrainz_data_find_child (RBMusicBrainzData *data, const char *attr, const char *value)
{
	GList *l;
	for (l = data->children; l != NULL; l = l->next) {
		RBMusicBrainzData *child = l->data;
		GQueue *d;
		GList *i;

		d = g_hash_table_lookup (child->attrs, attr);
		if (d == NULL)
			continue;
		for (i = d->head; i != NULL; i = i->next) {
			if (g_strcmp0 (value, i->data) == 0)
				return child;
		}
	}

	return NULL;
}

GList *
rb_musicbrainz_data_get_children (RBMusicBrainzData *data)
{
	return g_list_copy (data->children);
}

const char *
rb_musicbrainz_data_get_data_type (RBMusicBrainzData *data)
{
	return data->type;
}

static RBMusicBrainzData *
new_data (RBMusicBrainzData *parent, const char *type)
{
	RBMusicBrainzData *d = g_new0 (RBMusicBrainzData, 1);
	d->type = g_strdup (type);
	d->parent = parent;
	d->attrs = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, (GDestroyNotify)free_values);

	if (parent) {
		parent->children = g_list_append (parent->children, d);
	}
	return d;
}

static void
add_attr (RBMusicBrainzData *data, const char *attr, const char *value)
{
	GQueue *d;

	d = g_hash_table_lookup (data->attrs, attr);
	if (d == NULL) {
		d = g_queue_new ();
		g_hash_table_insert (data->attrs, (char *)attr, d);
	}

	g_queue_push_tail (d, g_strdup (value));
}

static char *
get_path (RBMusicBrainzParseContext *ctx)
{
	GString s = {0,};
	GList *l;

	for (l = ctx->current->path_start; l != NULL; l = l->next) {
		g_string_append (&s, "/");
		g_string_append (&s, l->data);
	}

	return s.str;
}

static void
start_element (GMarkupParseContext *pctx,
	       const gchar *element_name,
	       const gchar **attribute_names,
	       const gchar **attribute_values,
	       gpointer user_data,
	       GError **error)
{
	RBMusicBrainzParseContext *ctx = user_data;
	char *path;
	int i;
	int j;

	g_queue_push_tail (&ctx->path, g_strdup (element_name));

	for (i = 0; i < G_N_ELEMENTS (object_types); i++) {
		if (g_strcmp0 (element_name, object_types[i].name) == 0) {
			RBMusicBrainzData *d = new_data (ctx->current, element_name);
			d->path_start = ctx->path.tail;
			ctx->current = d;
			ctx->map = object_types[i].map;
			break;
		}
	}

	if (ctx->map == NULL)
		return;

	path = get_path (ctx);
	for (i = 0; ctx->map[i].path != NULL; i++) {
		if (g_strcmp0 (path, ctx->map[i].path) == 0) {
			if (ctx->map[i].xml_attr != NULL) {
				for (j = 0; attribute_names[j] != NULL; j++) {
					if (g_strcmp0 (attribute_names[j], ctx->map[i].xml_attr) == 0) {
						add_attr (ctx->current,
							  (char *)ctx->map[i].attr,
							  attribute_values[j]);
					}
				}
			} else {
				ctx->item = ctx->map[i].attr;
			}
			break;
		}
	}

	g_free (path);
}

static void
end_element (GMarkupParseContext *pctx,
	     const gchar *element_name,
	     gpointer user_data,
	     GError **error)
{
	RBMusicBrainzParseContext *ctx = user_data;

	if (ctx->item) {
		add_attr (ctx->current, (char *)ctx->item, ctx->text.str);
		ctx->item = NULL;
	}

	if (ctx->path.tail == ctx->current->path_start) {
		ctx->current->path_start = NULL;
		ctx->current = ctx->current->parent;
	}

	g_free (g_queue_pop_tail (&ctx->path));

	g_free (ctx->text.str);
	ctx->text.str = NULL;
	ctx->text.len = 0;
	ctx->text.allocated_len = 0;
}

static void
text (GMarkupParseContext *pctx,
      const gchar *text,
      gsize text_len,
      gpointer user_data,
      GError **error)
{
	RBMusicBrainzParseContext *ctx = user_data;

	if (ctx->item) {
		g_string_append (&ctx->text, text);
	}
}


RBMusicBrainzData *
rb_musicbrainz_data_parse (const char *data, gssize len, GError **error)
{
	RBMusicBrainzParseContext ctx;
	GMarkupParser parser = {
		start_element,
		end_element,
		text
	};
	GMarkupParseContext *pctx;

	ctx.root = new_data (NULL, "root");
	ctx.current = ctx.root;
	ctx.text.str = NULL;
	ctx.text.len = 0;
	ctx.text.allocated_len = 0;
	ctx.item = NULL;
	ctx.map = NULL;
	g_queue_init (&ctx.path);

	pctx = g_markup_parse_context_new (&parser, 0, &ctx, NULL);
	if (g_markup_parse_context_parse (pctx, data, len, error) == FALSE) {
		rb_musicbrainz_data_free (ctx.root);
		return NULL;
	}

	if (g_markup_parse_context_end_parse (pctx, error) == FALSE) {
		rb_musicbrainz_data_free (ctx.root);
		return NULL;
	}
	g_markup_parse_context_free (pctx);

	return ctx.root;
}

GList *
rb_musicbrainz_data_get_attr_values (RBMusicBrainzData *data, const char *attr)
{
	GQueue *d;
	d = g_hash_table_lookup (data->attrs, attr);
	if (d == NULL) {
		return NULL;
	}

	return g_list_copy (d->head);
}


static void
lookup_cb (SoupSession *session,
           GAsyncResult *soup_result,
           GSimpleAsyncResult *result)
{
	GBytes *bytes;
	const char *body;
	size_t body_size;
	SoupMessage *message;
	SoupStatus code;
	RBMusicBrainzData *data;
	GError *error = NULL;

	bytes = soup_session_send_and_read_finish (session, soup_result, NULL);
	if (bytes == NULL) {
		g_simple_async_result_set_error (result,
						 RB_MUSICBRAINZ_ERROR,
						 RB_MUSICBRAINZ_ERROR_SERVER,
						 _("Unable to connect to Musicbrainz server"));
	} else {
		body = g_bytes_get_data (bytes, &body_size);
		message = soup_session_get_async_result_message (session, soup_result);
		code = soup_message_get_status (message);

		if (code == SOUP_STATUS_NOT_FOUND || code == SOUP_STATUS_BAD_REQUEST)  {
			g_simple_async_result_set_error (result,
							 RB_MUSICBRAINZ_ERROR,
							 RB_MUSICBRAINZ_ERROR_NOT_FOUND,
							 _("Not found"));
		} else if (code != SOUP_STATUS_OK || body_size == 0) {
			g_simple_async_result_set_error (result,
							 RB_MUSICBRAINZ_ERROR,
							 RB_MUSICBRAINZ_ERROR_SERVER,
							 _("Musicbrainz server error"));
		} else {
			data = rb_musicbrainz_data_parse (body, (gssize)body_size, &error);
			if (data == NULL) {
				g_simple_async_result_set_from_error (result, error);
				g_clear_error (&error);
			} else {
				g_simple_async_result_set_op_res_gpointer (result, data, NULL);
			}
		}

		g_bytes_unref (bytes);
	}

	g_simple_async_result_complete (result);
	g_object_unref (result);
}

void
rb_musicbrainz_lookup (const char *entity,
		       const char *entity_id,
		       const char **includes,
		       GCancellable *cancellable,
		       GAsyncReadyCallback callback,
		       gpointer user_data)
{
	GSimpleAsyncResult *result;
	SoupMessage *message;
	SoupSession *session;
	char *uri_str;

	result = g_simple_async_result_new (NULL,
					    callback,
					    user_data,
					    rb_musicbrainz_lookup);
	g_simple_async_result_set_check_cancellable (result, cancellable);

	session = soup_session_new ();
	soup_session_set_user_agent (session, "Rhythmbox/" VERSION);

	uri_str = g_strdup_printf ("https://musicbrainz.org/ws/2/%s/%s", entity, entity_id);

	if (includes == NULL) {
		message = soup_message_new (SOUP_METHOD_GET, uri_str);
	} else {
		char *inc;
		char *query;

		inc = g_strjoinv ("+", (char **)includes);
		query = soup_form_encode ("inc", inc, NULL);
		g_free (inc);

		message = soup_message_new_from_encoded_form (SOUP_METHOD_GET, uri_str, query);
	}

	g_free (uri_str);
	g_return_if_fail (message != NULL);

	soup_session_send_and_read_async (session,
					  message,
					  G_PRIORITY_DEFAULT,
					  NULL,
					  (GAsyncReadyCallback) lookup_cb,
					  result);
}

RBMusicBrainzData *
rb_musicbrainz_lookup_finish (GAsyncResult *result,
			      GError **error)
{
	g_return_val_if_fail (g_simple_async_result_is_valid (result,
							      NULL,
							      rb_musicbrainz_lookup),
			      NULL);
	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return NULL;

	return g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));
}

char *
rb_musicbrainz_create_submit_url (const char *disc_id, const char *full_disc_id)
{
	char **bits;
	int *intbits;
	GString *url;
	int i;
	int n;

	/* full disc id is a space-delimited list of hex numbers.. */
	bits = g_strsplit (full_disc_id, " ", 0);
	n = g_strv_length (bits);
	intbits = g_new0 (int, n+1);
	for (i = 0; i < n; i++) {
		intbits[i] = strtol (bits[i], 0, 16);
	}
	g_strfreev (bits);

	url = g_string_new ("https://mm.musicbrainz.org/cdtoc/attach?id=");

	g_string_append (url, disc_id);		/* urlencode? */
	g_string_append_printf (url, "&tracks=%d&toc=%d", intbits[1], intbits[0]);

	/* .. that we put in the url in decimal */
	for (i = 1; i < n; i++) {
		g_string_append_printf (url, "+%d", intbits[i]);
	}
	
	g_free (intbits);

	return g_string_free (url, FALSE);
}
