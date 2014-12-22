/*
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
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

#ifndef __RB_SEARCH_ENTRY_H
#define __RB_SEARCH_ENTRY_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define RB_TYPE_SEARCH_ENTRY         (rb_search_entry_get_type ())
#define RB_SEARCH_ENTRY(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_SEARCH_ENTRY, RBSearchEntry))
#define RB_SEARCH_ENTRY_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_SEARCH_ENTRY, RBSearchEntryClass))
#define RB_IS_SEARCH_ENTRY(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_SEARCH_ENTRY))
#define RB_IS_SEARCH_ENTRY_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_SEARCH_ENTRY))
#define RB_SEARCH_ENTRY_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_SEARCH_ENTRY, RBSearchEntryClass))

typedef struct _RBSearchEntry RBSearchEntry;
typedef struct _RBSearchEntryClass RBSearchEntryClass;

typedef struct RBSearchEntryPrivate RBSearchEntryPrivate;

struct _RBSearchEntry
{
	GtkBox parent;

	RBSearchEntryPrivate *priv;
};

struct _RBSearchEntryClass
{
	GtkBoxClass parent;

	void (*search) (RBSearchEntry *view, const char *text);
	void (*activate) (RBSearchEntry *entry, const char *text);
	void (*show_popup) (RBSearchEntry *entry);
};

GType		rb_search_entry_get_type (void);

RBSearchEntry *	rb_search_entry_new      (gboolean has_popup);

void		rb_search_entry_clear    (RBSearchEntry *entry);

void		rb_search_entry_set_text (RBSearchEntry *entry, const char *text);

void		rb_search_entry_set_placeholder (RBSearchEntry *entry, const char *text);

gboolean	rb_search_entry_searching(RBSearchEntry *entry);

void		rb_search_entry_grab_focus (RBSearchEntry *entry);

void		rb_search_entry_set_mnemonic (RBSearchEntry *entry, gboolean enable);

G_END_DECLS

#endif /* __RB_SEARCH_ENTRY_H */
