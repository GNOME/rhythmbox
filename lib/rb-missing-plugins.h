/* rb-missing-plugins.h

   Copyright (C) 2007 Tim-Philipp Müller <tim centricular net>
   Copyright (C) 2007 Jonathan Matthew <jonathan@d14n.org>

   The Gnome Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the Gnome Library; see the file COPYING.LIB.  If not,
   see <http://www.gnu.org/licenses/>.

   Author: Tim-Philipp Müller <tim centricular net>
 */

#ifndef RB_MISSING_PLUGINS_H
#define RB_MISSING_PLUGINS_H

#include <shell/rb-shell.h>

G_BEGIN_DECLS

void rb_missing_plugins_init (GtkWindow *parent_window);

gboolean rb_missing_plugins_install (const char **details, gboolean ignore_blacklist, GClosure *closure);

G_END_DECLS

#endif /* RB_MISSING_PLUGINS_H */
