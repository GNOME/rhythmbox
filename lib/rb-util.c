/*
 *  arch-tag: Implementation of totally random functions that didn't fit elsewhere
 *
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
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

#include "rb-util.h"

gboolean
rb_true_function (gpointer dummy, ...)
{
	return TRUE;
}

gboolean
rb_false_function (gpointer dummy, ...)
{
	return FALSE;
}

int
rb_gvalue_compare (GValue *a, GValue *b)
{
	int retval;
	const char *stra, *strb;
	
	switch (G_VALUE_TYPE (a))
	{
	case G_TYPE_BOOLEAN:
		if (g_value_get_int (a) < g_value_get_int (b))
			retval = -1;
		else if (g_value_get_int (a) == g_value_get_int (b))
			retval = 0;
		else
			retval = 1;
		break;
	case G_TYPE_CHAR:
		if (g_value_get_char (a) < g_value_get_char (b))
			retval = -1;
		else if (g_value_get_char (a) == g_value_get_char (b))
			retval = 0;
		else
			retval = 1;
		break;
	case G_TYPE_UCHAR:
		if (g_value_get_uchar (a) < g_value_get_uchar (b))
			retval = -1;
		else if (g_value_get_uchar (a) == g_value_get_uchar (b))
			retval = 0;
		else
			retval = 1;
		break;
	case G_TYPE_INT:
		if (g_value_get_int (a) < g_value_get_int (b))
			retval = -1;
		else if (g_value_get_int (a) == g_value_get_int (b))
			retval = 0;
		else
			retval = 1;
		break;
	case G_TYPE_UINT:
		if (g_value_get_uint (a) < g_value_get_uint (b))
			retval = -1;
		else if (g_value_get_uint (a) == g_value_get_uint (b))
			retval = 0;
		else
			retval = 1;
		break;
	case G_TYPE_LONG:
		if (g_value_get_long (a) < g_value_get_long (b))
			retval = -1;
		else if (g_value_get_long (a) == g_value_get_long (b))
			retval = 0;
		else
			retval = 1;
		break;
	case G_TYPE_ULONG:
		if (g_value_get_ulong (a) < g_value_get_ulong (b))
			retval = -1;
		else if (g_value_get_ulong (a) == g_value_get_ulong (b))
			retval = 0;
		else
			retval = 1;
		break;
	case G_TYPE_INT64:
		if (g_value_get_int64 (a) < g_value_get_int64 (b))
			retval = -1;
		else if (g_value_get_int64 (a) == g_value_get_int64 (b))
			retval = 0;
		else
			retval = 1;
		break;
	case G_TYPE_UINT64:
		if (g_value_get_uint64 (a) < g_value_get_uint64 (b))
			retval = -1;
		else if (g_value_get_uint64 (a) == g_value_get_uint64 (b))
			retval = 0;
		else
			retval = 1;
		break;
	case G_TYPE_ENUM:
		/* this is somewhat bogus. */
		if (g_value_get_enum (a) < g_value_get_enum (b))
			retval = -1;
		else if (g_value_get_enum (a) == g_value_get_enum (b))
			retval = 0;
		else
			retval = 1;
		break;
	case G_TYPE_FLAGS:
		/* this is even more bogus. */
		if (g_value_get_flags (a) < g_value_get_flags (b))
			retval = -1;
		else if (g_value_get_flags (a) == g_value_get_flags (b))
			retval = 0;
		else
			retval = 1;
		break;
	case G_TYPE_FLOAT:
		if (g_value_get_float (a) < g_value_get_float (b))
			retval = -1;
		else if (g_value_get_float (a) == g_value_get_float (b))
			retval = 0;
		else
			retval = 1;
		break;
	case G_TYPE_DOUBLE:
		if (g_value_get_double (a) < g_value_get_double (b))
			retval = -1;
		else if (g_value_get_double (a) == g_value_get_double (b))
			retval = 0;
		else
			retval = 1;
		break;
	case G_TYPE_STRING:
		stra = g_value_get_string (a);
		strb = g_value_get_string (b);
		if (stra == NULL) stra = "";
		if (strb == NULL) strb = "";
		retval = g_utf8_collate (stra, strb);
		break;
	case G_TYPE_POINTER:
	case G_TYPE_BOXED:
	case G_TYPE_OBJECT:
	default:
		g_assert_not_reached ();
		retval = 0;
		break;
	}
	return retval;
}
