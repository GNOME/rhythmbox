/*
 *  arch-tag: Header files for new  podcast dialog
 *
 *  Copyright (C) 2005 Renato Araujo Oliveira Filho <renato.filho@indt.org.br>
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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#include <gtk/gtkdialog.h>
#include "rb-podcast-manager.h"

#ifndef __RB_NEW_PODCAST_DIALOG_H
#define __RB_NEW_PODCAST_DIALOG_H

G_BEGIN_DECLS

#define RB_TYPE_NEW_PODCAST_DIALOG         (rb_new_podcast_dialog_get_type ())
#define RB_NEW_PODCAST_DIALOG(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_NEW_PODCAST_DIALOG, RBNewPodcastDialog))
#define RB_NEW_PODCAST_DIALOG_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_NEW_PODCAST_DIALOG, RBNewPodcastDialogClass))
#define RB_IS_NEW_PODCAST_DIALOG(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_NEW_PODCAST_DIALOG))
#define RB_IS_NEW_PODCAST_DIALOG_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_NEW_PODCAST_DIALOG))
#define RB_NEW_PODCAST_DIALOG_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_NEW_PODCAST_DIALOG, RBNewPodcastDialogClass))

typedef struct RBNewPodcastDialogPrivate RBNewPodcastDialogPrivate;

typedef struct
{
	GtkDialog parent;

	RBNewPodcastDialogPrivate *priv;
} RBNewPodcastDialog;

typedef struct
{
	GtkDialogClass parent_class;
} RBNewPodcastDialogClass;

GType      rb_new_podcast_dialog_get_type (void);
GtkWidget* rb_new_podcast_dialog_new      (RBPodcastManager *pd);

G_END_DECLS

#endif /* __RB_NEW_PODCAST_DIALOG_H */
