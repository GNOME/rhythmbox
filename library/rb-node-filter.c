/* 
 *  Copyright (C) 2002 Olivier Martin <omartin@ifrance.com>
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

#include <stdlib.h>
#include <string.h>

#include "rb-debug.h"
#include "rb-node-filter.h"
#include "rb-node-song.h"

static void rb_node_filter_class_init (RBNodeFilterClass *klass);
static void rb_node_filter_init (RBNodeFilter *node);
static void rb_node_filter_finalize (GObject *object);
static void rb_node_filter_set_object_property (GObject *object,
						guint prop_id,
						const GValue *value,
						GParamSpec *pspec);
static void rb_node_filter_get_object_property (GObject *object, 
						guint prop_id,
						GValue *value,
						GParamSpec *pspec);
static gpointer thread_main (RBNodeFilterPrivate *priv);


typedef enum
{
	LAST_SIGNAL
} RBNodeFilterSignal;

struct RBNodeFilterPrivate
{
	RBLibrary *library;

	GMutex *expr_lock;
	gboolean expr_changed;
	char *expression;

	GMutex *lock;
	GThread *thread;
	gboolean dead;
	gboolean done;

	RBNode *root;
};

enum
{
	PROP_0,
	PROP_EXPRESSION,
	PROP_LIBRARY
};

static GObjectClass *parent_class = NULL;

GType
rb_node_filter_get_type (void)
{
	static GType rb_node_filter_type = 0;

	if (rb_node_filter_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBNodeFilterClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_node_filter_class_init,
			NULL,
			NULL,
			sizeof (RBNodeFilter),
			0,
			(GInstanceInitFunc) rb_node_filter_init
		};

		rb_node_filter_type = g_type_register_static (G_TYPE_OBJECT,
							      "RBNodeFilter",
							      &our_info, 0);
	}

	return rb_node_filter_type;
}

static void
rb_node_filter_class_init (RBNodeFilterClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_node_filter_finalize;

	object_class->set_property = rb_node_filter_set_object_property;
	object_class->get_property = rb_node_filter_get_object_property;

	g_object_class_install_property (object_class,
					 PROP_EXPRESSION,
					 g_param_spec_string ("expression", 
							      "Expression string", 
							      "Expression string", 
							      NULL,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_LIBRARY,
					 g_param_spec_object ("library", 
							      "Library object", 
							      "Library object", 
							      RB_TYPE_LIBRARY,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

}

static void
rb_node_filter_init (RBNodeFilter *filter)
{
	filter->priv = g_new0 (RBNodeFilterPrivate, 1);

	g_assert (filter->priv != NULL);

	filter->priv->root = rb_node_new ();

	filter->priv->lock = g_mutex_new ();
	filter->priv->expr_lock = g_mutex_new ();
}

static void
rb_node_filter_finalize (GObject *object)
{
	RBNodeFilter *filter;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_NODE_FILTER (object));

	filter = RB_NODE_FILTER (object);

	g_return_if_fail (filter->priv != NULL);

	g_mutex_lock (filter->priv->lock);
	filter->priv->dead = TRUE;
	g_mutex_unlock (filter->priv->lock);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_node_filter_set_object_property (GObject *object,
		                    guint prop_id,
		                    const GValue *value,
		                    GParamSpec *pspec)
{
	RBNodeFilter *filter = RB_NODE_FILTER (object);

	switch (prop_id)
	{
	case PROP_EXPRESSION:
		{
			GPtrArray *kids;
			int i;

			kids = rb_node_get_children (filter->priv->root);
			for (i = kids->len - 1; i >= 0; i--)
				rb_node_remove_child (filter->priv->root,
						      g_ptr_array_index (kids, i));
			rb_node_thaw (filter->priv->root);
			filter->priv->done = FALSE;

			/* set the new expression to search */
			g_mutex_lock (filter->priv->expr_lock);
			if (filter->priv->expression != NULL)
				g_free (filter->priv->expression);
			filter->priv->expression = g_strdup (g_value_get_string (value));
			filter->priv->expr_changed = TRUE;
			g_mutex_unlock (filter->priv->expr_lock);
		}
		break;
	case PROP_LIBRARY:
		{
			filter->priv->library = g_value_get_object (value);

			filter->priv->thread = g_thread_create ((GThreadFunc) thread_main,
								filter->priv, TRUE, NULL);
			g_thread_set_priority (filter->priv->thread,
					       G_THREAD_PRIORITY_LOW);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_node_filter_get_object_property (GObject *object,
		                    guint prop_id,
		                    GValue *value,
		                    GParamSpec *pspec)
{
	RBNodeFilter *filter = RB_NODE_FILTER (object);

	switch (prop_id)
	{
	case PROP_EXPRESSION:
		{
			g_mutex_lock (filter->priv->expr_lock);
			g_value_set_string (value, filter->priv->expression);
			g_mutex_unlock (filter->priv->expr_lock);
		}
		break;
	case PROP_LIBRARY:
		{
			g_value_set_object (value, filter->priv->library);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}


RBNodeFilter *
rb_node_filter_new (RBLibrary *library)
{
	RBNodeFilter *filter;

	filter = RB_NODE_FILTER (g_object_new (RB_TYPE_NODE_FILTER,
					       "library", library,
					       NULL));

	g_return_val_if_fail (filter->priv != NULL, NULL);

	return filter;
}

RBNode *
rb_node_filter_get_root (RBNodeFilter *filter)
{
	g_return_val_if_fail (filter != NULL, NULL);

	return filter->priv->root;
}

void
rb_node_filter_set_expression (RBNodeFilter *filter,
			       const char *expression)
{
	g_return_if_fail (filter != NULL);
	g_return_if_fail (expression != NULL);

	g_object_set (G_OBJECT (filter),
		      "expression", expression,
		      NULL);
}

void 
rb_node_filter_abort_search (RBNodeFilter *filter)
{
	g_return_if_fail (filter != NULL);

	g_mutex_lock (filter->priv->expr_lock);
	filter->priv->expr_changed = TRUE;
	g_mutex_unlock (filter->priv->expr_lock);
}

static gpointer
thread_main (RBNodeFilterPrivate *priv)
{
	while (TRUE)
	{
		RBNode *node;
		GPtrArray *kids;
		char *expression = NULL;
		int results = 0, i;
		RBProfiler *p;

		g_mutex_lock (priv->lock);
		if (priv->dead == TRUE)
		{
			g_mutex_unlock (priv->lock);
			g_mutex_free (priv->lock);
			g_mutex_free (priv->expr_lock);
			g_free (priv->expression);
			g_free (priv);
			g_thread_exit (NULL);
		}
		g_mutex_unlock (priv->lock);
	
		if ((priv->expression == NULL) ||
		    (priv->done == TRUE))
		{
			g_mutex_unlock (priv->lock);
			g_usleep (10);
			continue;
		}

		/* Check the expression is correct */
		g_mutex_lock (priv->expr_lock);
		if (priv->expression != NULL)
			expression = g_utf8_casefold (priv->expression, -1);
		g_strstrip (expression);
		priv->expr_changed = FALSE;
		g_mutex_unlock (priv->expr_lock);

		/* start finding nodes */
		node = rb_library_get_all_songs (priv->library);
		kids = rb_node_get_children (node);

		p = rb_profiler_new ("Searching ...");
		for (i = 0; i < kids->len; i++)
		{
			char *title, *artist, *album;
			gboolean found = FALSE;
			gboolean aborted = FALSE;

			/* verify if search has been aborted */
			g_mutex_lock (priv->expr_lock);
			aborted = priv->expr_changed;
			g_mutex_unlock (priv->expr_lock);

			if (aborted == TRUE)
				break;

			/* check the title - artist - album */
			node = g_ptr_array_index (kids, i);
			
			title = g_utf8_casefold (rb_node_get_property_string (node, RB_NODE_PROP_NAME), -1);
			if (strstr (title, expression) != NULL)
				found = TRUE;
			g_free (title);

			if (found == FALSE)
			{
				artist = g_utf8_casefold (rb_node_get_property_string (node, RB_NODE_SONG_PROP_ARTIST), -1);
				if (strstr (artist, expression) != NULL)
					found = TRUE;
				g_free (artist);
			}

			if (found == FALSE)
			{
				album = g_utf8_casefold (rb_node_get_property_string (node, RB_NODE_SONG_PROP_ALBUM), -1);
				if (strstr (album, expression) != NULL)
					found = TRUE;
				g_free (album);
			}

			if (found == TRUE)
			{
				rb_node_add_child (priv->root, node);
				results++;
			}
		}

		rb_node_thaw (node);

		g_free (expression);

		priv->done = TRUE;

		rb_profiler_dump (p);
		rb_profiler_free (p);
	}

	return NULL;
}
