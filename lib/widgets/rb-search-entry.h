/*
 *  arch-tag: Header for search entry widget
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
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

#ifndef __RB_SEARCH_ENTRY_H
#define __RB_SEARCH_ENTRY_H

#include <gtk/gtkhbox.h>

G_BEGIN_DECLS

#define RB_TYPE_SEARCH_ENTRY         (rb_search_entry_get_type ())
#define RB_SEARCH_ENTRY(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_SEARCH_ENTRY, RBSearchEntry))
#define RB_SEARCH_ENTRY_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_SEARCH_ENTRY, RBSearchEntryClass))
#define RB_IS_SEARCH_ENTRY(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_SEARCH_ENTRY))
#define RB_IS_SEARCH_ENTRY_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_SEARCH_ENTRY))
#define RB_SEARCH_ENTRY_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_SEARCH_ENTRY, RBSearchEntryClass))

typedef struct RBSearchEntryPrivate RBSearchEntryPrivate;

typedef struct
{
	GtkHBox parent;

	RBSearchEntryPrivate *priv;
} RBSearchEntry;

typedef struct
{
	GtkHBoxClass parent;

	void (*search) (RBSearchEntry *view, const char *text);
} RBSearchEntryClass;

GType		rb_search_entry_get_type (void);

RBSearchEntry *	rb_search_entry_new      (void);

void		rb_search_entry_clear    (RBSearchEntry *entry);

gboolean	rb_search_entry_searching(RBSearchEntry *entry);

G_END_DECLS

#endif /* __RB_SEARCH_ENTRY_H */
