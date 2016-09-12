/*
 *  Copyright (C) 2009 Paul Bellamy  <paul.a.bellamy@gmail.com>
 *  Copyright (C) 2009 Jonathan Matthew <jonathan@d14n.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

/*
 * sync settings consist of categories and groups within those categories.
 * categories are things like music and podcasts, groups are things like
 * playlists and individual podcast feeds.  if sync for a category is enabled,
 * then all groups within the category will be synced.
 *
 * at some point we probably need to have more than just enabled/disabled settings
 * at all levels.. things like the number of episodes for a podcast feed to keep
 * on the device.
 *
 * categories are stored as groups in the keyfile, where the keyfile group name
 * matches the category name.  if the category as a whole is enabled, the
 * 'enabled' key will be set in the keyfile group.  the list of groups enabled
 * within the category is stored in the 'groups' key in the keyfile group.
 *
 * if any settings exist for a sync group, they will be stored in a keyfile group
 * named <category>:<sync-group>.  there is no way to set any such settings at
 * present.
 */

#include "config.h"

#include <gio/gio.h>
#include <string.h>

#include "rb-sync-settings.h"
#include "rb-debug.h"
#include "rb-util.h"

#define CATEGORY_ENABLED_KEY	"enabled"
#define CATEGORY_ALL_GROUPS_KEY "all-groups"
#define CATEGORY_GROUPS_KEY	"groups"

typedef struct {
	GKeyFile *key_file;
	char *key_file_path;

	guint save_key_file_id;
} RBSyncSettingsPrivate;

enum {
	PROP_0,
	PROP_KEYFILE_PATH
};

enum {
	UPDATED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (RBSyncSettings, rb_sync_settings, G_TYPE_OBJECT)

#define GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_SYNC_SETTINGS, RBSyncSettingsPrivate))

RBSyncSettings *
rb_sync_settings_new (const char *keyfile)
{
	GObject *settings;
	settings = g_object_new (RB_TYPE_SYNC_SETTINGS,
				 "keyfile-path", keyfile,
				 NULL);
	return RB_SYNC_SETTINGS (settings);
}

gboolean
rb_sync_settings_save (RBSyncSettings *settings)
{
	RBSyncSettingsPrivate *priv = GET_PRIVATE (settings);
	char *data;
	gsize length;
	GError *error = NULL;
	GFile *file;

	data = g_key_file_to_data (priv->key_file, &length, &error);
	if (error != NULL) {
		rb_debug ("unable to save sync settings: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	file = g_file_new_for_path (priv->key_file_path);
	g_file_replace_contents (file, data, length, NULL, FALSE, G_FILE_CREATE_NONE, NULL, NULL, &error);
	if (error != NULL) {
		rb_debug ("unable to save sync settings: %s", error->message);
		g_error_free (error);
	}
	g_object_unref (file);
	g_free (data);
	return (error == NULL);
}

static gboolean
_save_idle_cb (RBSyncSettings *settings)
{
	RBSyncSettingsPrivate *priv = GET_PRIVATE (settings);
	priv->save_key_file_id = 0;
	rb_sync_settings_save (settings);

	g_signal_emit (settings, signals[UPDATED], 0);
	return FALSE;
}

static void
_save_idle (RBSyncSettings *settings)
{
	RBSyncSettingsPrivate *priv = GET_PRIVATE (settings);
	if (priv->save_key_file_id == 0) {
		priv->save_key_file_id = g_idle_add ((GSourceFunc) _save_idle_cb, settings);
	}
}

static gboolean
_get_boolean_with_default (GKeyFile *keyfile, const char *group, const char *key, gboolean default_value)
{
	GError *error = NULL;
	gboolean v;
	v = g_key_file_get_boolean (keyfile, group, key, &error);
	if (error != NULL) {
		g_error_free (error);
		return default_value;
	}
	return v;
}

void
rb_sync_settings_set_category (RBSyncSettings *settings,
					    const char *category,
					    gboolean enabled)
{
	RBSyncSettingsPrivate *priv = GET_PRIVATE (settings);
	g_key_file_set_boolean (priv->key_file, category, CATEGORY_ENABLED_KEY, enabled);
	_save_idle (settings);
}

gboolean
rb_sync_settings_sync_category (RBSyncSettings *settings,
					     const char *category)
{
	RBSyncSettingsPrivate *priv = GET_PRIVATE (settings);
	return _get_boolean_with_default (priv->key_file, category, CATEGORY_ENABLED_KEY, FALSE);
}

GList *
rb_sync_settings_get_enabled_categories (RBSyncSettings *settings)
{
	RBSyncSettingsPrivate *priv = GET_PRIVATE (settings);
	char **groups;
	GList *categories;
	int i;

	categories = NULL;
	groups = g_key_file_get_groups (priv->key_file, NULL);
	for (i = 0; groups[i] != NULL; i++) {
		/* filter out group entries */
		if (g_utf8_strchr (groups[i], -1, ':') != NULL) {
			continue;
		}

		categories = g_list_prepend (categories, g_strdup (groups[i]));
	}
	g_strfreev (groups);
	return g_list_reverse (categories);
}

void
rb_sync_settings_set_group (RBSyncSettings *settings,
					 const char *category,
					 const char *group,
					 gboolean enabled)
{
	RBSyncSettingsPrivate *priv = GET_PRIVATE (settings);
	char **groups;
	int ngroups;

	ngroups = 0;
	groups = g_key_file_get_string_list (priv->key_file, category, CATEGORY_GROUPS_KEY, NULL, NULL);

	if (groups != NULL) {
		int i;
		ngroups = g_strv_length (groups);

		for (i = 0; i < ngroups; i++) {
			if (strcmp (groups[i], group) == 0) {
				if (enabled) {
					return;
				} else {
					char *t;
					t = groups[i];
					groups[i] = groups[ngroups-1];
					groups[ngroups-1] = t;
					ngroups--;
				}
			}
		}
	}

	if (enabled) {
		groups = g_realloc (groups, (ngroups+2) * sizeof(char *));
		if (ngroups > 0 && groups[ngroups] != NULL) {
			g_free (groups[ngroups]);
		}
		groups[ngroups] = g_strdup (group);
		groups[ngroups+1] = NULL;
		ngroups++;
	}

	if (ngroups == 0) {
		g_key_file_remove_key (priv->key_file, category, CATEGORY_GROUPS_KEY, NULL);
	} else {
		g_key_file_set_string_list (priv->key_file, category, CATEGORY_GROUPS_KEY, (const char * const *)groups, ngroups);
	}
	g_strfreev (groups);

	_save_idle (settings);
}

gboolean
rb_sync_settings_group_enabled (RBSyncSettings *settings,
					     const char *category,
					     const char *group)
{
	RBSyncSettingsPrivate *priv = GET_PRIVATE (settings);
	char **groups;
	int i;
	gboolean found = FALSE;

	groups = g_key_file_get_string_list (priv->key_file, category, CATEGORY_GROUPS_KEY, NULL, NULL);
	if (groups == NULL) {
		return FALSE;
	}

	for (i = 0; groups[i] != NULL; i++) {
		if (strcmp (groups[i], group) == 0) {
			found = TRUE;
			break;
		}
	}

	g_strfreev (groups);
	return found;
}

gboolean
rb_sync_settings_sync_group (RBSyncSettings *settings,
					  const char *category,
					  const char *group)
{
	if (rb_sync_settings_sync_category (settings, category) == TRUE) {
		return TRUE;
	}
	return rb_sync_settings_group_enabled (settings, category, group);
}

gboolean
rb_sync_settings_has_enabled_groups (RBSyncSettings *settings,
						  const char *category)
{
	RBSyncSettingsPrivate *priv = GET_PRIVATE (settings);
	char **groups;

	groups = g_key_file_get_string_list (priv->key_file, category, CATEGORY_GROUPS_KEY, NULL, NULL);
	if (groups == NULL) {
		return FALSE;
	}

	g_strfreev (groups);
	return TRUE;
}

GList *
rb_sync_settings_get_enabled_groups (RBSyncSettings *settings,
						  const char *category)
{
	RBSyncSettingsPrivate *priv = GET_PRIVATE (settings);
	char **groups;
	GList *glist = NULL;
	int i;

	groups = g_key_file_get_string_list (priv->key_file, category, CATEGORY_GROUPS_KEY, NULL, NULL);
	if (groups == NULL) {
		return NULL;
	}

	for (i = 0; groups[i] != NULL; i++) {
		glist = g_list_prepend (glist, g_strdup (groups[i]));
	}

	g_strfreev (groups);
	return g_list_reverse (glist);
}

void
rb_sync_settings_clear_groups (RBSyncSettings *settings, const char *category)
{
	RBSyncSettingsPrivate *priv = GET_PRIVATE (settings);
	g_key_file_remove_key (priv->key_file, category, CATEGORY_GROUPS_KEY, NULL);
	_save_idle (settings);
}



static void
rb_sync_settings_init (RBSyncSettings *settings)
{
	/* nothing */
}

static void
impl_constructed (GObject *object)
{
	RBSyncSettingsPrivate *priv = GET_PRIVATE (object);
	GError *error = NULL;

	priv->key_file = g_key_file_new ();
	if (g_key_file_load_from_file (priv->key_file,
				       priv->key_file_path,
				       G_KEY_FILE_KEEP_COMMENTS,
				       &error) == FALSE) {
		rb_debug ("unable to load sync settings from %s: %s", priv->key_file_path, error->message);
		g_error_free (error);

		/* probably need a way to set defaults.. syncing nothing by default
		 * is kind of boring.  used to default to syncing all music and all
		 * podcasts.
		 */
	}

	RB_CHAIN_GOBJECT_METHOD(rb_sync_settings_parent_class, constructed, object);
}

static void
impl_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	RBSyncSettingsPrivate *priv = GET_PRIVATE (object);
	switch (prop_id) {
	case PROP_KEYFILE_PATH:
		priv->key_file_path = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	RBSyncSettingsPrivate *priv = GET_PRIVATE (object);
	switch (prop_id) {
	case PROP_KEYFILE_PATH:
		g_value_set_string (value, priv->key_file_path);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_dispose (GObject *object)
{
	RBSyncSettingsPrivate *priv = GET_PRIVATE (object);

	/* if a save is pending, do it now */
	if (priv->save_key_file_id != 0) {
		g_source_remove (priv->save_key_file_id);
		priv->save_key_file_id = 0;
		rb_sync_settings_save (RB_SYNC_SETTINGS (object));
	}

	G_OBJECT_CLASS (rb_sync_settings_parent_class)->dispose (object);
}

static void
impl_finalize (GObject *object)
{
	RBSyncSettingsPrivate *priv = GET_PRIVATE (object);

	g_key_file_free (priv->key_file);
	g_free (priv->key_file_path);

	G_OBJECT_CLASS (rb_sync_settings_parent_class)->finalize (object);
}

static void
rb_sync_settings_class_init (RBSyncSettingsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = impl_finalize;
	object_class->dispose = impl_dispose;
	object_class->constructed = impl_constructed;

	object_class->set_property = impl_set_property;
	object_class->get_property = impl_get_property;

	g_object_class_install_property (object_class,
					 PROP_KEYFILE_PATH,
					 g_param_spec_string ("keyfile-path",
							      "keyfile path",
							      "path to the key file storing the sync settings",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	signals[UPDATED] = g_signal_new ("updated",
					 RB_TYPE_SYNC_SETTINGS,
					 G_SIGNAL_RUN_LAST,
					 G_STRUCT_OFFSET (RBSyncSettingsClass, updated),
					 NULL, NULL,
					 NULL,
					 G_TYPE_NONE,
					 0);
	g_type_class_add_private (object_class, sizeof (RBSyncSettingsPrivate));
}
