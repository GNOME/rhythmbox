/*
 *  Copyright (C) 2010 Jonathan Matthew <jonathan@d14n.org>
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

#ifndef RB_DISPLAY_PAGE_H
#define RB_DISPLAY_PAGE_H

#include <gtk/gtk.h>

#include <shell/rb-shell-preferences.h>

G_BEGIN_DECLS

typedef struct _RBDisplayPage		RBDisplayPage;
typedef struct _RBDisplayPageClass	RBDisplayPageClass;
typedef struct _RBDisplayPagePrivate	RBDisplayPagePrivate;

#define RB_TYPE_DISPLAY_PAGE (rb_display_page_get_type ())
#define RB_DISPLAY_PAGE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_DISPLAY_PAGE, RBDisplayPage))
#define RB_DISPLAY_PAGE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_DISPLAY_PAGE, RBDisplayPageClass))
#define RB_IS_DISPLAY_PAGE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_DISPLAY_PAGE))
#define RB_IS_DISPLAY_PAGE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_DISPLAY_PAGE))
#define RB_DISPLAY_PAGE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_DISPLAY_PAGE, RBDisplayPageClass))

#define RB_DISPLAY_PAGE_ICON_SIZE	GTK_ICON_SIZE_MENU

struct _RBDisplayPage
{
	GtkBox parent;

	RBDisplayPagePrivate *priv;
};

struct _RBDisplayPageClass
{
	GtkBoxClass parent_class;

	/* signals */
	void	(*status_changed)	(RBDisplayPage *page);
	void	(*deleted)		(RBDisplayPage *page);

	/* methods */
	gboolean (*selectable)		(RBDisplayPage *page);
	void	(*selected)		(RBDisplayPage *page);
	void	(*deselected)		(RBDisplayPage *page);
	void	(*activate)		(RBDisplayPage *page);

	GtkWidget *(*get_config_widget)	(RBDisplayPage *page, RBShellPreferences *prefs);

	void	(*get_status)		(RBDisplayPage *page, char **text, gboolean *busy);
	gboolean (*receive_drag)	(RBDisplayPage *page, GtkSelectionData *data);
	void	(*delete_thyself)	(RBDisplayPage *page);

	gboolean (*can_remove)		(RBDisplayPage *page);
	void	(*remove)		(RBDisplayPage *page);
};

GType		rb_display_page_get_type		(void);

gboolean	rb_display_page_receive_drag		(RBDisplayPage *page, GtkSelectionData *data);

gboolean	rb_display_page_selectable		(RBDisplayPage *page);
void		rb_display_page_selected		(RBDisplayPage *page);
void		rb_display_page_deselected		(RBDisplayPage *page);
void		rb_display_page_activate		(RBDisplayPage *page);

GtkWidget *	rb_display_page_get_config_widget	(RBDisplayPage *page, RBShellPreferences *prefs);
void		rb_display_page_get_status		(RBDisplayPage *page, char **text, gboolean *busy);

void		rb_display_page_delete_thyself		(RBDisplayPage *page);

gboolean	rb_display_page_can_remove		(RBDisplayPage *page);
void		rb_display_page_remove			(RBDisplayPage *page);

void		rb_display_page_set_icon_name		(RBDisplayPage *page, const char *icon_name);

/* things for display page implementations */

void		rb_display_page_notify_status_changed	(RBDisplayPage *page);

void		_rb_add_display_page_actions 		(GActionMap *map,
							 GObject *shell,
							 const GActionEntry *actions,
							 int num_actions);

/* things for the display page model */

void		_rb_display_page_add_pending_child	(RBDisplayPage *page, RBDisplayPage *child);
GList *		_rb_display_page_get_pending_children	(RBDisplayPage *page);

#endif /* RB_DISPLAY_PAGE_H */
