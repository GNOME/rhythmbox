/*
 *  Copyright (C) 2012 Jonathan Matthew  <jonathan@d14n.org>
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

#ifndef RB_IMPORT_DIALOG_H
#define RB_IMPORT_DIALOG_H

#include <gtk/gtk.h>

#include <shell/rb-shell.h>

G_BEGIN_DECLS

#define RB_TYPE_IMPORT_DIALOG         (rb_import_dialog_get_type ())
#define RB_IMPORT_DIALOG(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_IMPORT_DIALOG, RBImportDialog))
#define RB_IMPORT_DIALOG_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_IMPORT_DIALOG, RBImportDialogClass))
#define RB_IS_IMPORT_DIALOG(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_IMPORT_DIALOG))
#define RB_IS_IMPORT_DIALOG_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_IMPORT_DIALOG))
#define RB_IMPORT_DIALOG_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_IMPORT_DIALOG, RBImportDialogClass))

typedef struct _RBImportDialog RBImportDialog;
typedef struct _RBImportDialogClass RBImportDialogClass;

typedef struct RBImportDialogPrivate RBImportDialogPrivate;

struct _RBImportDialog
{
	GtkGrid parent;

	RBImportDialogPrivate *priv;
};

struct _RBImportDialogClass
{
	GtkGridClass parent;

	/* signals */
	void	(*close)	(RBImportDialog *dialog);
	void	(*closed)	(RBImportDialog *dialog);
};

GType		rb_import_dialog_get_type		(void);

GtkWidget *     rb_import_dialog_new			(RBShell *shell);

void		rb_import_dialog_reset			(RBImportDialog *dialog);

G_END_DECLS

#endif /* RB_IMPORT_DIALOG_H */
