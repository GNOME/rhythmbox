/*
 *  arch-tag: Implementation of metadata reading using GStreamer
 *
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
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
#include <libgnome/gnome-i18n.h>
#include <gst/gst.h>
#include <gst/gsttag.h>
#include <string.h>

#include "rb-metadata.h"
#include "rb-debug.h"

static void rb_metadata_class_init (RBMetaDataClass *klass);
static void rb_metadata_init (RBMetaData *md);
static void rb_metadata_finalize (GObject *object);

struct RBMetaDataPrivate
{
	char *uri;
  
	GHashTable *metadata;

	GstElement *pipeline;
};

static GObjectClass *parent_class = NULL;

GType
rb_metadata_get_type (void)
{
	static GType type = 0;

	if (type == 0) {
		static const GTypeInfo our_info =
		{
			sizeof (RBMetaDataClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_metadata_class_init,
			NULL,
			NULL,
			sizeof (RBMetaData),
			0,
			(GInstanceInitFunc) rb_metadata_init,
		};

		type = g_type_register_static (G_TYPE_OBJECT,
					       "RBMetaData",
					       &our_info, 0);
	}

	return type;
}

static void
rb_metadata_class_init (RBMetaDataClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_metadata_finalize;
}

static void
rb_metadata_init (RBMetaData *md)
{
	md->priv = g_new0 (RBMetaDataPrivate, 1);
}

static void
rb_metadata_finalize (GObject *object)
{
	RBMetaData *md;

	md = RB_METADATA (object);

	g_hash_table_destroy (md->priv->metadata);

	if (md->priv->pipeline)
		gst_object_unref (GST_OBJECT (md->priv->pipeline));

	g_free (md->priv->uri);

	g_free (md->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

RBMetaData *
rb_metadata_new (void)
{
	return RB_METADATA (g_object_new (RB_TYPE_METADATA, NULL));
}

GQuark
rb_metadata_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("rb_metadata_error");

	return quark;
}

static void
free_gvalue (GValue *val)
{
	g_value_unset (val);
	g_free (val);
}

static void
rb_metadata_gst_eos_cb (GstElement *element,
			gboolean *eos)
{
	*eos = TRUE;
}

static int
rb_metadata_gst_tag_to_field (const char *tag)
{
	if (!strcmp (tag, GST_TAG_TITLE))
		return RB_METADATA_FIELD_TITLE;
	else if (!strcmp (tag, GST_TAG_ARTIST))
		return RB_METADATA_FIELD_ARTIST;
	else if (!strcmp (tag, GST_TAG_ALBUM))
		return RB_METADATA_FIELD_ALBUM;
	else if (!strcmp (tag, GST_TAG_DATE))
		return RB_METADATA_FIELD_DATE;
	else if (!strcmp (tag, GST_TAG_GENRE))
		return RB_METADATA_FIELD_GENRE;
	else if (!strcmp (tag, GST_TAG_COMMENT))
		return RB_METADATA_FIELD_COMMENT;
	else if (!strcmp (tag, GST_TAG_TRACK_NUMBER))
		return RB_METADATA_FIELD_TRACK_NUMBER;
	else if (!strcmp (tag, GST_TAG_LOCATION))
		return RB_METADATA_FIELD_LOCATION;
	else if (!strcmp (tag, GST_TAG_DESCRIPTION))
		return RB_METADATA_FIELD_DESCRIPTION;
	else if (!strcmp (tag, GST_TAG_VERSION))
		return RB_METADATA_FIELD_VERSION;
	else if (!strcmp (tag, GST_TAG_ISRC))
		return RB_METADATA_FIELD_ISRC;
	else if (!strcmp (tag, GST_TAG_ORGANIZATION))
		return RB_METADATA_FIELD_ORGANIZATION;
	else if (!strcmp (tag, GST_TAG_COPYRIGHT))
		return RB_METADATA_FIELD_COPYRIGHT;
	else if (!strcmp (tag, GST_TAG_CONTACT))
		return RB_METADATA_FIELD_CONTACT;
	else if (!strcmp (tag, GST_TAG_LICENSE))
		return RB_METADATA_FIELD_LICENSE;
	else if (!strcmp (tag, GST_TAG_PERFORMER))
		return RB_METADATA_FIELD_PERFORMER;
	else if (!strcmp (tag, GST_TAG_DURATION))
		return RB_METADATA_FIELD_DURATION;
	else if (!strcmp (tag, GST_TAG_CODEC))
		return RB_METADATA_FIELD_CODEC;
	else if (!strcmp (tag, GST_TAG_BITRATE))
		return RB_METADATA_FIELD_BITRATE;
	else
		return -1;
}

static void
rb_metadata_gst_load_tag (const GstTagList *list, const gchar *tag, RBMetaData *md)
{
  int count, tem;
  RBMetaDataField field;
  GValue *newval;
  const GValue *val;

  count = gst_tag_list_get_tag_size (list, tag);
  if (count < 1)
	  return;

  tem = rb_metadata_gst_tag_to_field (tag);
  if (tem < 0)
	  return;
  field = (RBMetaDataField) tem;

  val = gst_tag_list_get_value_index (list, tag, 0);
  newval = g_new0 (GValue, 1);
  if (field == RB_METADATA_FIELD_TRACK_NUMBER) {
	  g_value_init (newval, G_TYPE_INT);
  } else {
	  g_value_init (newval, G_VALUE_TYPE (val));
  }
  if (g_value_transform (val, newval))
	  g_hash_table_insert (md->priv->metadata, GINT_TO_POINTER (field),
			       newval);
  else
	  rb_debug ("Could not transform tag value type %s into %s",
		    g_type_name (G_VALUE_TYPE (val)), g_type_name (G_VALUE_TYPE (newval)));
}

static void
rb_metadata_gst_found_tag (GObject *pipeline, GstElement *source, GstTagList *tags, RBMetaData *md)
{
	gst_tag_list_foreach (tags, (GstTagForeachFunc) rb_metadata_gst_load_tag, md);
}

void
rb_metadata_load (RBMetaData *md,
		  const char *uri,
		  GError **error)
{
	GstElement *pipeline = NULL;
	GstElement *gnomevfssrc = NULL;
	GstElement *typefind = NULL;
	GstElement *spider = NULL;
	GstElement *fakesink = NULL;
	g_free (md->priv->uri);
	md->priv->uri = NULL;

	if (md->priv->pipeline)
		gst_object_unref (GST_OBJECT (md->priv->pipeline));

	if (uri == NULL) {
		md->priv->pipeline = NULL;
		return;
	}

	if (md->priv->metadata)
		g_hash_table_destroy (md->priv->metadata);
	md->priv->metadata = g_hash_table_new_full (g_direct_hash, g_direct_equal,
						    NULL, (GDestroyNotify) free_gvalue);

	/* The main tagfinding pipeline looks like this:
	 * gnomevfssrc ! typefind ! spider ! application/x-gst-tags ! fakesink
	 */
	pipeline = gst_pipeline_new ("pipeline");

	g_signal_connect (pipeline, "found-tag", G_CALLBACK (rb_metadata_gst_found_tag), md);

#define MAKE_ADD_PLUGIN_OR_ERROR(VAR, NAME) \
	VAR = gst_element_factory_make (NAME, NAME); \
	if (VAR == NULL) { \
		g_set_error (error, \
			     RB_METADATA_ERROR, \
			     RB_METADATA_ERROR_MISSING_PLUGIN, \
			     _("Failed to create %s element; check your installation"), \
			     NAME); \
		gst_object_unref (GST_OBJECT (pipeline)); \
		md->priv->pipeline = NULL; \
		return; \
	} \
	gst_bin_add (GST_BIN (pipeline), VAR);		  

	MAKE_ADD_PLUGIN_OR_ERROR(gnomevfssrc, "gnomevfssrc")
	g_object_set (G_OBJECT (gnomevfssrc),
			       "location", uri, NULL);
	MAKE_ADD_PLUGIN_OR_ERROR(typefind, "typefind")
	MAKE_ADD_PLUGIN_OR_ERROR(spider, "spider")
	MAKE_ADD_PLUGIN_OR_ERROR(fakesink, "fakesink")
#undef MAKE_ADD_PLUGIN_OR_ERROR

	gst_element_link_many (gnomevfssrc, typefind, spider, NULL);
	gst_element_link_filtered (spider, fakesink,
				   gst_caps_new ("app filter", "application/x-gst-tags",
						 gst_props_empty_new ()));

	g_signal_connect (G_OBJECT (fakesink), "eos",
			  G_CALLBACK (rb_metadata_gst_eos_cb), md);
	md->priv->pipeline = pipeline;
	gst_element_set_state (pipeline, GST_STATE_PLAYING);
	while (gst_bin_iterate (GST_BIN (pipeline)))
		;
}

gboolean
rb_metadata_can_save (RBMetaData *md)
{
	return FALSE;
}

void
rb_metadata_save (RBMetaData *md, GError **error)
{
	g_set_error (error,
		     RB_METADATA_ERROR,
		     RB_METADATA_ERROR_UNSUPPORTED,
		     _("Operation not supported"));
}

gboolean
rb_metadata_get (RBMetaData *md, RBMetaDataField field,
		 GValue *ret)
{
	GValue *val;
	if ((val = g_hash_table_lookup (md->priv->metadata,
					GINT_TO_POINTER (field)))) {
		g_value_init (ret, G_VALUE_TYPE (val));
		g_value_copy (val, ret);
		return TRUE;
	}
	return FALSE;
}

gboolean
rb_metadata_set (RBMetaData *md, RBMetaDataField field,
		 GValue *val)
{
	return FALSE;
}

