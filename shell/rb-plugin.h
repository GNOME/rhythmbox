/*
 * heavily based on code from Gedit
 *
 * Copyright (C) 2002-2005 - Paolo Maggi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301  USA.
 */

#ifndef __RB_PLUGIN_H__
#define __RB_PLUGIN_H__

#include <glib-object.h>

#include <shell/rb-shell.h>

G_BEGIN_DECLS

/*
 * Type checking and casting macros
 */
#define RB_TYPE_PLUGIN              (rb_plugin_get_type())
#define RB_PLUGIN(obj)              (G_TYPE_CHECK_INSTANCE_CAST((obj), RB_TYPE_PLUGIN, RBPlugin))
#define RB_PLUGIN_CONST(obj)        (G_TYPE_CHECK_INSTANCE_CAST((obj), RB_TYPE_PLUGIN, RBPlugin const))
#define RB_PLUGIN_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST((klass), RB_TYPE_PLUGIN, RBPluginClass))
#define RB_IS_PLUGIN(obj)           (G_TYPE_CHECK_INSTANCE_TYPE((obj), RB_TYPE_PLUGIN))
#define RB_IS_PLUGIN_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), RB_TYPE_PLUGIN))
#define RB_PLUGIN_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS((obj), RB_TYPE_PLUGIN, RBPluginClass))

/*
 * Main object structure
 */

typedef struct _RBPlugin RBPlugin;
typedef struct _RBPluginClass RBPluginClass;

struct _RBPlugin
{
	GObject parent;
};

typedef void		(*RBPluginActivationFunc)	(RBPlugin *plugin, RBShell *shell);
typedef GtkWidget *	(*RBPluginWidgetFunc)		(RBPlugin *plugin);
typedef gboolean	(*RBPluginBooleanFunc)		(RBPlugin *plugin);

/*
 * Class definition
 */
struct _RBPluginClass
{
	GObjectClass parent_class;

	/* Virtual public methods */

	RBPluginActivationFunc		activate;
	RBPluginActivationFunc		deactivate;
	RBPluginWidgetFunc		create_configure_dialog;

	/* Plugins should not override this, it's handled automatically by
	   the RbPluginClass */
	RBPluginBooleanFunc		is_configurable;
};

/*
 * Public methods
 */
GType 		 rb_plugin_get_type 		(void) G_GNUC_CONST;

void 		 rb_plugin_activate		(RBPlugin *plugin,
						 RBShell *shell);
void 		 rb_plugin_deactivate		(RBPlugin *plugin,
						 RBShell *shell);

gboolean	 rb_plugin_is_configurable	(RBPlugin *plugin);
GtkWidget	*rb_plugin_create_configure_dialog
						(RBPlugin *plugin);

char *		 rb_plugin_find_file		(RBPlugin *plugin,
						 const char *file);

GList *          rb_get_plugin_paths            (void);

/*
 * Utility macro used to register plugins
 *
 * use: RB_PLUGIN_REGISTER(RBSamplePlugin, rb_sample_plugin)
 */

#define RB_PLUGIN_REGISTER(PluginName, plugin_name)				\
										\
static GType plugin_name##_type = 0;						\
static GTypeModule *plugin_module_type = 0;		                \
										\
GType										\
plugin_name##_get_type (void)							\
{										\
	return plugin_name##_type;						\
}										\
										\
static void     plugin_name##_init              (PluginName        *self);	\
static void     plugin_name##_class_init        (PluginName##Class *klass);	\
static gpointer plugin_name##_parent_class = NULL;				\
static void     plugin_name##_class_intern_init (gpointer klass)		\
{										\
	plugin_name##_parent_class = g_type_class_peek_parent (klass);		\
	plugin_name##_class_init ((PluginName##Class *) klass);			\
}										\
										\
G_MODULE_EXPORT GType								\
register_rb_plugin (GTypeModule *module)					\
{										\
	const GTypeInfo our_info =						\
	{									\
		sizeof (PluginName##Class),					\
		NULL, /* base_init */						\
		NULL, /* base_finalize */					\
		(GClassInitFunc) plugin_name##_class_intern_init,		\
		NULL,								\
		NULL, /* class_data */						\
		sizeof (PluginName),						\
		0, /* n_preallocs */						\
		(GInstanceInitFunc) plugin_name##_init				\
	};									\
										\
	rb_debug ("Registering plugin %s", #PluginName);			\
										\
	/* Initialise the i18n stuff */						\
	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);			\
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");			\
										\
	plugin_module_type = module;						\
	plugin_name##_type = g_type_module_register_type (module,		\
					    RB_TYPE_PLUGIN,			\
					    #PluginName,			\
					    &our_info,				\
					    0);					\
	return plugin_name##_type;						\
}

#define RB_PLUGIN_REGISTER_TYPE(type_name)                                      \
        type_name##_register_type (plugin_module_type)

#define RB_PLUGIN_DEFINE_TYPE(TypeName, type_name, TYPE_PARENT)			\
static void type_name##_init (TypeName *self); 					\
static void type_name##_class_init (TypeName##Class *klass); 			\
static gpointer type_name##_parent_class = ((void *)0); 			\
static GType type_name##_type_id = 0;						\
										\
static void 									\
type_name##_class_intern_init (gpointer klass) 					\
{ 										\
	type_name##_parent_class = g_type_class_peek_parent (klass);		\
	type_name##_class_init ((TypeName##Class*) klass); 			\
}										\
										\
										\
GType 										\
type_name##_get_type (void)							\
{										\
	g_assert (type_name##_type_id != 0);					\
										\
	return type_name##_type_id;						\
}										\
										\
GType 										\
type_name##_register_type (GTypeModule *module) 				\
{ 										\
										\
	if ((type_name##_type_id == 0)) { 					\
		const GTypeInfo g_define_type_info = { 				\
			sizeof (TypeName##Class), 				\
			(GBaseInitFunc) ((void *)0), 				\
			(GBaseFinalizeFunc) ((void *)0), 			\
			(GClassInitFunc) type_name##_class_intern_init, 	\
			(GClassFinalizeFunc) ((void *)0), 			\
			((void *)0), 						\
			sizeof (TypeName), 					\
			0, 							\
			(GInstanceInitFunc) type_name##_init,			\
			((void *)0) 						\
		}; 								\
		type_name##_type_id = 						\
			g_type_module_register_type (module, 			\
						     TYPE_PARENT, 		\
						     #TypeName,			\
						     &g_define_type_info, 	\
						     (GTypeFlags) 0); 		\
	} 									\
										\
	return type_name##_type_id;						\
}

G_END_DECLS

#endif  /* __RB_PLUGIN_H__ */
