/* 
   arch-tag: Header for Rhythmbox playlist parser

   Copyright (C) 2002, 2003 Bastien Nocera <hadess@hadess.net>
   Copyright (C) 2003 Colin Walters <walters@verbum.org>

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
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.

   Author: Bastien Nocera <hadess@hadess.net>
 */

#ifndef TOTEM_PL_PARSER_H
#define TOTEM_PL_PARSER_H

#include <glib.h>
#include <gtk/gtktreemodel.h>

G_BEGIN_DECLS

#define TOTEM_TYPE_PL_PARSER            (totem_pl_parser_get_type ())
#define TOTEM_PL_PARSER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), TOTEM_TYPE_PL_PARSER, TotemPlParser))
#define TOTEM_PL_PARSER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), TOTEM_TYPE_PL_PARSER, TotemPlParserClass))
#define TOTEM_IS_PL_PARSER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TOTEM_TYPE_PL_PARSER))
#define TOTEM_IS_PL_PARSER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), TOTEM_TYPE_PL_PARSER))

typedef enum
{
	TOTEM_PL_PARSER_RESULT_UNHANDLED,
	TOTEM_PL_PARSER_RESULT_ERROR,
	TOTEM_PL_PARSER_RESULT_SUCCESS
} TotemPlParserResult;

typedef struct TotemPlParser	       TotemPlParser;
typedef struct TotemPlParserClass      TotemPlParserClass;
typedef struct TotemPlParserPrivate    TotemPlParserPrivate;

struct TotemPlParser {
	GObject parent;
	TotemPlParserPrivate *priv;
};

struct TotemPlParserClass {
	GObjectClass parent_class;

	/* signals */
	void (*entry) (TotemPlParser *parser, const char *uri, const char *title,
		       const char *genre);
};

typedef enum
{
	TOTEM_PL_PARSER_PLS,
	TOTEM_PL_PARSER_M3U,
	TOTEM_PL_PARSER_M3U_DOS,
} TotemPlParserType;

typedef enum
{
	TOTEM_PL_PARSER_ERROR_VFS_OPEN,
	TOTEM_PL_PARSER_ERROR_VFS_WRITE,
} TotemPlParserError;

#define TOTEM_PL_PARSER_ERROR (totem_pl_parser_error_quark ())

GQuark totem_pl_parser_error_quark (void);

typedef void (*TotemPlParserIterFunc) (GtkTreeModel *model, GtkTreeIter *iter, char **uri, char **title);

GtkType    totem_pl_parser_get_type (void);

gboolean   totem_pl_parser_write (TotemPlParser *parser, GtkTreeModel *model,
				  TotemPlParserIterFunc func,
				  const char *output, TotemPlParserType type,
				  GError **error);
void	   totem_pl_parser_add_ignored_scheme (TotemPlParser *parser,
					       const char *scheme);
TotemPlParserResult totem_pl_parser_parse (TotemPlParser *parser, const char *url, gboolean fallback);

TotemPlParser *totem_pl_parser_new (void);

G_END_DECLS

#endif /* TOTEM_PL_PARSER_H */
