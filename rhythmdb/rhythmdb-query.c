/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2003,2004 Colin Walters <walters@gnome.org>
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

#include <config.h>

#include <string.h>

#include <libxml/entities.h>
#include <glib.h>
#include <glib-object.h>
#include <gobject/gvaluecollector.h>

#include "rhythmdb.h"
#include "rhythmdb-private.h"
#include "rb-util.h"

#define RB_PARSE_CONJ (xmlChar *) "conjunction"
#define RB_PARSE_SUBQUERY (xmlChar *) "subquery"
#define RB_PARSE_LIKE (xmlChar *) "like"
#define RB_PARSE_PROP (xmlChar *) "prop"
#define RB_PARSE_NOT_LIKE (xmlChar *) "not-like"
#define RB_PARSE_PREFIX (xmlChar *) "prefix"
#define RB_PARSE_SUFFIX (xmlChar *) "suffix"
#define RB_PARSE_EQUALS (xmlChar *) "equals"
#define RB_PARSE_NOT_EQUAL (xmlChar *) "not-equal"
#define RB_PARSE_DISJ (xmlChar *) "disjunction"
#define RB_PARSE_GREATER (xmlChar *) "greater"
#define RB_PARSE_LESS (xmlChar *) "less"
#define RB_PARSE_CURRENT_TIME_WITHIN (xmlChar *) "current-time-within"
#define RB_PARSE_CURRENT_TIME_NOT_WITHIN (xmlChar *) "current-time-not-within"
#define RB_PARSE_YEAR_EQUALS RB_PARSE_EQUALS
#define RB_PARSE_YEAR_NOT_EQUAL RB_PARSE_NOT_EQUAL
#define RB_PARSE_YEAR_GREATER RB_PARSE_GREATER
#define RB_PARSE_YEAR_LESS RB_PARSE_LESS

/**
 * rhythmdb_query_copy:
 * @array: the query to copy.
 *
 * Creates a copy of a query.
 *
 * Return value: (transfer full): a copy of the passed query.
 * It must be freed with rhythmdb_query_free()
 **/
GPtrArray *
rhythmdb_query_copy (GPtrArray *array)
{
	GPtrArray *ret;

	if (!array)
		return NULL;

	ret = g_ptr_array_sized_new (array->len);
	rhythmdb_query_concatenate (ret, array);

	return ret;
}

/**
 * rhythmdb_query_concatenate:
 * @query1: query to append to
 * @query2: query to append
 *
 * Appends @query2 to @query1.
 */
void
rhythmdb_query_concatenate (GPtrArray *query1, GPtrArray *query2)
{
	guint i;

	g_assert (query2);
	if (!query2)
		return;

	for (i = 0; i < query2->len; i++) {
		RhythmDBQueryData *data = g_ptr_array_index (query2, i);
		RhythmDBQueryData *new_data = g_new0 (RhythmDBQueryData, 1);
		new_data->type = data->type;
		new_data->propid = data->propid;
		if (data->val) {
			new_data->val = g_new0 (GValue, 1);
			g_value_init (new_data->val, G_VALUE_TYPE (data->val));
			g_value_copy (data->val, new_data->val);
		}
		if (data->subquery)
			new_data->subquery = rhythmdb_query_copy (data->subquery);
		g_ptr_array_add (query1, new_data);
	}
}

/**
 * rhythmdb_query_parse_valist:
 * @db: the #RhythmDB
 * @args: the arguments to parse
 *
 * Converts a va_list into a parsed query in the form of a @GPtrArray.
 * See @rhythmdb_query_parse for more information on the parsing process.
 *
 * Return value: converted query
 */
GPtrArray *
rhythmdb_query_parse_valist (RhythmDB *db, va_list args)
{
	RhythmDBQueryType query;
	GPtrArray *ret = g_ptr_array_new ();
	char *error;

	while ((query = va_arg (args, RhythmDBQueryType)) != RHYTHMDB_QUERY_END) {
		RhythmDBQueryData *data = g_new0 (RhythmDBQueryData, 1);
		data->type = query;
		switch (query) {
		case RHYTHMDB_QUERY_DISJUNCTION:
			break;
		case RHYTHMDB_QUERY_SUBQUERY:
			data->subquery = rhythmdb_query_copy (va_arg (args, GPtrArray *));
			break;
		case RHYTHMDB_QUERY_PROP_EQUALS:
		case RHYTHMDB_QUERY_PROP_NOT_EQUAL:
		case RHYTHMDB_QUERY_PROP_LIKE:
		case RHYTHMDB_QUERY_PROP_NOT_LIKE:
		case RHYTHMDB_QUERY_PROP_PREFIX:
		case RHYTHMDB_QUERY_PROP_SUFFIX:
		case RHYTHMDB_QUERY_PROP_GREATER:
		case RHYTHMDB_QUERY_PROP_LESS:
		case RHYTHMDB_QUERY_PROP_CURRENT_TIME_WITHIN:
		case RHYTHMDB_QUERY_PROP_CURRENT_TIME_NOT_WITHIN:
		case RHYTHMDB_QUERY_PROP_YEAR_EQUALS:
		case RHYTHMDB_QUERY_PROP_YEAR_NOT_EQUAL:
		case RHYTHMDB_QUERY_PROP_YEAR_GREATER:
		case RHYTHMDB_QUERY_PROP_YEAR_LESS:
			data->propid = va_arg (args, guint);
			data->val = g_new0 (GValue, 1);
			g_value_init (data->val, rhythmdb_get_property_type (db, data->propid));
			G_VALUE_COLLECT (data->val, args, 0, &error);
			break;
		case RHYTHMDB_QUERY_END:
			g_assert_not_reached ();
			break;
		}
		g_ptr_array_add (ret, data);
	}
	return ret;
}

/**
 * rhythmdb_query_parse:
 * @db: a #RhythmDB instance
 * @...: query criteria to parse
 *
 * Creates a query from a list of criteria.
 *
 * Most criteria consists of an operator (#RhythmDBQueryType),
 * a property (#RhythmDBPropType) and the data to compare with. An entry
 * matches a criteria if the operator returns true with the value of the
 * entries property as the first argument, and the given data as the second
 * argument.
 *
 * Three types criteria are special. Passing RHYTHMDB_QUERY_END indicates the
 * end of the list of criteria, and must be the last passes parameter.
 *
 * The second special criteria is a subquery which is defined by passing
 * RHYTHMDB_QUERY_SUBQUERY, followed by a query (#GPtrArray). An entry will
 * match a subquery criteria if it matches all criteria in the subquery.
 *
 * The third special criteria is a disjunction which is defined by passing
 * RHYTHMDB_QUERY_DISJUNCTION, which will make an entry match the query if
 * it matches the criteria before the disjunction, the criteria after the
 * disjunction, or both.
 *
 * Example:
 * 	rhythmdb_query_parse (db,
 * 		RHYTHMDB_QUERY_SUBQUERY, subquery,
 * 		RHYTHMDB_QUERY_DISJUNCTION
 * 		RHYTHMDB_QUERY_PROP_LIKE, RHYTHMDB_PROP_TITLE, "cat",
 *		RHYTHMDB_QUERY_DISJUNCTION
 *		RHYTHMDB_QUERY_PROP_GREATER, RHYTHMDB_PROP_RATING, 2.5,
 *		RHYTHMDB_QUERY_PROP_LESS, RHYTHMDB_PROP_PLAY_COUNT, 10,
 * 		RHYTHMDB_QUERY_END);
 *
 * 	will create a query that matches entries:
 * 	a) that match the query "subquery", or
 * 	b) that have "cat" in their title, or
 * 	c) have a rating of at least 2.5, and a play count of at most 10
 *
 * Returns: (transfer full): a the newly created query.
 * It must be freed with rhythmdb_query_free()
 **/
GPtrArray *
rhythmdb_query_parse (RhythmDB *db, ...)
{
	GPtrArray *ret;
	va_list args;

	va_start (args, db);

	ret = rhythmdb_query_parse_valist (db, args);

	va_end (args);

	return ret;
}

/**
 * rhythmdb_query_append:
 * @db: a #RhythmDB instance
 * @query: a query.
 * @...: query criteria to append
 *
 * Appends new criteria to the query @query.
 *
 * The list of criteria must be in the same format as for rhythmdb_query_parse,
 * and ended by RHYTHMDB_QUERY_END.
 **/
void
rhythmdb_query_append (RhythmDB *db, GPtrArray *query, ...)
{
	va_list args;
	guint i;
	GPtrArray *new = g_ptr_array_new ();

	va_start (args, query);

	new = rhythmdb_query_parse_valist (db, args);

	for (i = 0; i < new->len; i++)
		g_ptr_array_add (query, g_ptr_array_index (new, i));

	g_ptr_array_free (new, TRUE);

	va_end (args);
}

/**
 * rhythmdb_query_append_params:
 * @db: the #RhythmDB
 * @query: the query to append to
 * @type: query type
 * @prop: query property
 * @value: query value
 *
 * Appends a new query term to @query.
 */
void
rhythmdb_query_append_params (RhythmDB *db, GPtrArray *query,
			      RhythmDBQueryType type, RhythmDBPropType prop, const GValue *value)
{
	RhythmDBQueryData *data = g_new0 (RhythmDBQueryData, 1);

	data->type = type;
	switch (type) {
	case RHYTHMDB_QUERY_END:
		g_assert_not_reached ();
		break;
	case RHYTHMDB_QUERY_DISJUNCTION:
		break;
	case RHYTHMDB_QUERY_SUBQUERY:
		data->subquery = rhythmdb_query_copy (g_value_get_pointer (value));
		break;
	case RHYTHMDB_QUERY_PROP_EQUALS:
	case RHYTHMDB_QUERY_PROP_NOT_EQUAL:
	case RHYTHMDB_QUERY_PROP_LIKE:
	case RHYTHMDB_QUERY_PROP_NOT_LIKE:
	case RHYTHMDB_QUERY_PROP_PREFIX:
	case RHYTHMDB_QUERY_PROP_SUFFIX:
	case RHYTHMDB_QUERY_PROP_GREATER:
	case RHYTHMDB_QUERY_PROP_LESS:
	case RHYTHMDB_QUERY_PROP_CURRENT_TIME_WITHIN:
	case RHYTHMDB_QUERY_PROP_CURRENT_TIME_NOT_WITHIN:
	case RHYTHMDB_QUERY_PROP_YEAR_EQUALS:
	case RHYTHMDB_QUERY_PROP_YEAR_NOT_EQUAL:
	case RHYTHMDB_QUERY_PROP_YEAR_GREATER:
	case RHYTHMDB_QUERY_PROP_YEAR_LESS:
		data->propid = prop;
		data->val = g_new0 (GValue, 1);
		g_value_init (data->val, rhythmdb_get_property_type (db, data->propid));
		g_value_transform (value, data->val);
		break;
	}

	g_ptr_array_add (query, data);
}

/**
 * rhythmdb_query_free:
 * @query: a query.
 *
 * Frees the query @query
 */
void
rhythmdb_query_free (GPtrArray *query)
{
	guint i;

	if (query == NULL)
		return;

	for (i = 0; i < query->len; i++) {
		RhythmDBQueryData *data = g_ptr_array_index (query, i);
		switch (data->type) {
		case RHYTHMDB_QUERY_DISJUNCTION:
			break;
		case RHYTHMDB_QUERY_SUBQUERY:
			rhythmdb_query_free (data->subquery);
			break;
		case RHYTHMDB_QUERY_PROP_EQUALS:
		case RHYTHMDB_QUERY_PROP_NOT_EQUAL:
		case RHYTHMDB_QUERY_PROP_LIKE:
		case RHYTHMDB_QUERY_PROP_NOT_LIKE:
		case RHYTHMDB_QUERY_PROP_PREFIX:
		case RHYTHMDB_QUERY_PROP_SUFFIX:
		case RHYTHMDB_QUERY_PROP_GREATER:
		case RHYTHMDB_QUERY_PROP_LESS:
		case RHYTHMDB_QUERY_PROP_CURRENT_TIME_WITHIN:
		case RHYTHMDB_QUERY_PROP_CURRENT_TIME_NOT_WITHIN:
		case RHYTHMDB_QUERY_PROP_YEAR_EQUALS:
		case RHYTHMDB_QUERY_PROP_YEAR_NOT_EQUAL:
		case RHYTHMDB_QUERY_PROP_YEAR_GREATER:
		case RHYTHMDB_QUERY_PROP_YEAR_LESS:
			g_value_unset (data->val);
			g_free (data->val);
			break;
		case RHYTHMDB_QUERY_END:
			g_assert_not_reached ();
			break;
		}
		g_free (data);
	}

	g_ptr_array_free (query, TRUE);
}

static char *
prop_gvalue_to_string (RhythmDB *db,
		       RhythmDBPropType propid,
		       GValue *val)
{
	/* special-case some properties */
	switch (propid) {
	case RHYTHMDB_PROP_TYPE:
		{
			RhythmDBEntryType *type = g_value_get_object (val);
			return g_strdup (rhythmdb_entry_type_get_name (type));
		}
		break;
	default:
		break;
	}

	/* otherwise just convert numbers to strings */
	switch (G_VALUE_TYPE (val)) {
	case G_TYPE_STRING:
		return g_value_dup_string (val);
	case G_TYPE_BOOLEAN:
		return g_strdup_printf ("%d", g_value_get_boolean (val));
	case G_TYPE_INT:
		return g_strdup_printf ("%d", g_value_get_int (val));
	case G_TYPE_LONG:
		return g_strdup_printf ("%ld", g_value_get_long (val));
	case G_TYPE_ULONG:
		return g_strdup_printf ("%lu", g_value_get_ulong (val));
	case G_TYPE_UINT64:
		return g_strdup_printf ("%" G_GUINT64_FORMAT, g_value_get_uint64 (val));
	case G_TYPE_FLOAT:
		return g_strdup_printf ("%f", g_value_get_float (val));
	case G_TYPE_DOUBLE:
		return g_strdup_printf ("%f", g_value_get_double (val));
	default:
		g_assert_not_reached ();
		return NULL;
	}
}

static void
write_encoded_gvalue (RhythmDB *db,
		      xmlNodePtr node,
		      RhythmDBPropType propid,
		      GValue *val)
{
	char *strval = NULL;
	xmlChar *quoted;

	strval = prop_gvalue_to_string (db, propid, val);
	quoted = xmlEncodeEntitiesReentrant (NULL, BAD_CAST strval);
	g_free (strval);

	xmlNodeSetContent (node, quoted);
	g_free (quoted);
}

/**
 * rhythmdb_read_encoded_property:
 * @db: the #RhythmDB
 * @content: encoded property value
 * @propid: property ID
 * @val: returns the property value
 *
 * Converts a string containing a property value into a #GValue
 * containing the native form of the property value.  For boolean
 * and numeric properties, this converts the string to a number.
 * For #RHYTHMDB_PROP_TYPE, this looks up the entry type by name.
 * For strings, no conversion is required.
 */
void
rhythmdb_read_encoded_property (RhythmDB *db,
				const char *content,
				RhythmDBPropType propid,
				GValue *val)
{
	g_value_init (val, rhythmdb_get_property_type (db, propid));

	switch (G_VALUE_TYPE (val)) {
	case G_TYPE_STRING:
		g_value_set_string (val, content);
		break;
	case G_TYPE_BOOLEAN:
		g_value_set_boolean (val, g_ascii_strtoull (content, NULL, 10));
		break;
	case G_TYPE_ULONG:
		g_value_set_ulong (val, g_ascii_strtoull (content, NULL, 10));
		break;
	case G_TYPE_UINT64:
		g_value_set_uint64 (val, g_ascii_strtoull (content, NULL, 10));
		break;
	case G_TYPE_DOUBLE:
		{
			gdouble d;
			char *end;

			d = g_ascii_strtod (content, &end);
			if (*end != '\0') {
				/* conversion wasn't entirely successful.
				 * try locale-aware strtod().
				 */
				d = strtod (content, NULL);
			}
			g_value_set_double (val, d);
		}
		break;
	case G_TYPE_OBJECT:			/* hm, really? */
		if (propid == RHYTHMDB_PROP_TYPE) {
			RhythmDBEntryType *entry_type;
			entry_type = rhythmdb_entry_type_get_by_name (db, content);
			if (entry_type != NULL) {
				g_value_set_object (val, entry_type);
				break;
			} else {
				g_warning ("Unexpected entry type");
				/* Fall through */
			}
		}
		/* Falling through on purpose to get an assert for unexpected
		 * cases
		 */
	default:
		g_warning ("Attempt to read '%s' of unhandled type %s",
			   rhythmdb_nice_elt_name_from_propid (db, propid),
			   g_type_name (G_VALUE_TYPE (val)));
		g_assert_not_reached ();
		break;
	}
}

/**
 * rhythmdb_query_serialize:
 * @db: the #RhythmDB
 * @query: query to serialize
 * @parent: XML node to attach the query to
 *
 * Converts @query into XML form as a child of @parent.  It can be converted
 * back into a query by passing @parent to @rhythmdb_query_deserialize.
 */
void
rhythmdb_query_serialize (RhythmDB *db, GPtrArray *query,
			  xmlNodePtr parent)
{
	guint i;
	xmlNodePtr node = xmlNewChild (parent, NULL, RB_PARSE_CONJ, NULL);
	xmlNodePtr subnode;

	for (i = 0; i < query->len; i++) {
		RhythmDBQueryData *data = g_ptr_array_index (query, i);

		switch (data->type) {
		case RHYTHMDB_QUERY_SUBQUERY:
			subnode = xmlNewChild (node, NULL, RB_PARSE_SUBQUERY, NULL);
			rhythmdb_query_serialize (db, data->subquery, subnode);
			break;
		case RHYTHMDB_QUERY_PROP_LIKE:
			subnode = xmlNewChild (node, NULL, RB_PARSE_LIKE, NULL);
			xmlSetProp (subnode, RB_PARSE_PROP, rhythmdb_nice_elt_name_from_propid (db, data->propid));
			write_encoded_gvalue (db, subnode, data->propid, data->val);
			break;
		case RHYTHMDB_QUERY_PROP_NOT_LIKE:
			subnode = xmlNewChild (node, NULL, RB_PARSE_NOT_LIKE, NULL);
			xmlSetProp (subnode, RB_PARSE_PROP, rhythmdb_nice_elt_name_from_propid (db, data->propid));
			write_encoded_gvalue (db, subnode, data->propid, data->val);
			break;
		case RHYTHMDB_QUERY_PROP_PREFIX:
			subnode = xmlNewChild (node, NULL, RB_PARSE_PREFIX, NULL);
			xmlSetProp (subnode, RB_PARSE_PROP, rhythmdb_nice_elt_name_from_propid (db, data->propid));
			write_encoded_gvalue (db, subnode, data->propid, data->val);
			break;
		case RHYTHMDB_QUERY_PROP_SUFFIX:
			subnode = xmlNewChild (node, NULL, RB_PARSE_SUFFIX, NULL);
			xmlSetProp (subnode, RB_PARSE_PROP, rhythmdb_nice_elt_name_from_propid (db, data->propid));
			write_encoded_gvalue (db, subnode, data->propid, data->val);
			break;
		case RHYTHMDB_QUERY_PROP_EQUALS:
			subnode = xmlNewChild (node, NULL, RB_PARSE_EQUALS, NULL);
			xmlSetProp (subnode, RB_PARSE_PROP, rhythmdb_nice_elt_name_from_propid (db, data->propid));
			write_encoded_gvalue (db, subnode, data->propid, data->val);
			break;
		case RHYTHMDB_QUERY_PROP_NOT_EQUAL:
			subnode = xmlNewChild (node, NULL, RB_PARSE_NOT_EQUAL, NULL);
			xmlSetProp (subnode, RB_PARSE_PROP, rhythmdb_nice_elt_name_from_propid (db, data->propid));
			write_encoded_gvalue (db, subnode, data->propid, data->val);
			break;
		case RHYTHMDB_QUERY_PROP_YEAR_EQUALS:
			subnode = xmlNewChild (node, NULL, RB_PARSE_YEAR_EQUALS, NULL);
			xmlSetProp (subnode, RB_PARSE_PROP, rhythmdb_nice_elt_name_from_propid (db, data->propid));
			write_encoded_gvalue (db, subnode, data->propid, data->val);
			break;
		case RHYTHMDB_QUERY_PROP_YEAR_NOT_EQUAL:
			subnode = xmlNewChild (node, NULL, RB_PARSE_YEAR_NOT_EQUAL, NULL);
			xmlSetProp (subnode, RB_PARSE_PROP, rhythmdb_nice_elt_name_from_propid (db, data->propid));
			write_encoded_gvalue (db, subnode, data->propid, data->val);
			break;
		case RHYTHMDB_QUERY_DISJUNCTION:
			subnode = xmlNewChild (node, NULL, RB_PARSE_DISJ, NULL);
			break;
		case RHYTHMDB_QUERY_END:
			break;
		case RHYTHMDB_QUERY_PROP_GREATER:
			subnode = xmlNewChild (node, NULL, RB_PARSE_GREATER, NULL);
			xmlSetProp (subnode, RB_PARSE_PROP, rhythmdb_nice_elt_name_from_propid (db, data->propid));
			write_encoded_gvalue (db, subnode, data->propid, data->val);
			break;
		case RHYTHMDB_QUERY_PROP_YEAR_GREATER:
			subnode = xmlNewChild (node, NULL, RB_PARSE_YEAR_GREATER, NULL);
			xmlSetProp (subnode, RB_PARSE_PROP, rhythmdb_nice_elt_name_from_propid (db, data->propid));
			write_encoded_gvalue (db, subnode, data->propid, data->val);
			break;
		case RHYTHMDB_QUERY_PROP_LESS:
			subnode = xmlNewChild (node, NULL, RB_PARSE_LESS, NULL);
			xmlSetProp (subnode, RB_PARSE_PROP, rhythmdb_nice_elt_name_from_propid (db, data->propid));
			write_encoded_gvalue (db, subnode, data->propid, data->val);
			break;
		case RHYTHMDB_QUERY_PROP_YEAR_LESS:
			subnode = xmlNewChild (node, NULL, RB_PARSE_YEAR_LESS, NULL);
			xmlSetProp (subnode, RB_PARSE_PROP, rhythmdb_nice_elt_name_from_propid (db, data->propid));
			write_encoded_gvalue (db, subnode, data->propid, data->val);
			break;
		case RHYTHMDB_QUERY_PROP_CURRENT_TIME_WITHIN:
			subnode = xmlNewChild (node, NULL, RB_PARSE_CURRENT_TIME_WITHIN, NULL);
			xmlSetProp (subnode, RB_PARSE_PROP, rhythmdb_nice_elt_name_from_propid (db, data->propid));
			write_encoded_gvalue (db, subnode, data->propid, data->val);
			break;
		case RHYTHMDB_QUERY_PROP_CURRENT_TIME_NOT_WITHIN:
			subnode = xmlNewChild (node, NULL, RB_PARSE_CURRENT_TIME_NOT_WITHIN, NULL);
			xmlSetProp (subnode, RB_PARSE_PROP, rhythmdb_nice_elt_name_from_propid (db, data->propid));
			write_encoded_gvalue (db, subnode, data->propid, data->val);
			break;
		}
	}
}

/**
 * rhythmdb_query_deserialize:
 * @db: the #RhythmDB
 * @parent: parent XML node of serialized query
 *
 * Converts a serialized query back into a @GPtrArray query.
 *
 * Return value: (transfer full): deserialized query.
 */
GPtrArray *
rhythmdb_query_deserialize (RhythmDB *db, xmlNodePtr parent)
{
	GPtrArray *query = g_ptr_array_new ();
	xmlNodePtr child;

	g_assert (!xmlStrcmp (parent->name, RB_PARSE_CONJ));

	for (child = parent->children; child; child = child->next) {
		RhythmDBQueryData *data;

		if (xmlNodeIsText (child))
			continue;

		data = g_new0 (RhythmDBQueryData, 1);

		if (!xmlStrcmp (child->name, RB_PARSE_SUBQUERY)) {
			xmlNodePtr subquery;
			data->type = RHYTHMDB_QUERY_SUBQUERY;
			subquery = child->children;
			while (xmlNodeIsText (subquery))
				subquery = subquery->next;

			data->subquery = rhythmdb_query_deserialize (db, subquery);
		} else if (!xmlStrcmp (child->name, RB_PARSE_DISJ)) {
			data->type = RHYTHMDB_QUERY_DISJUNCTION;
		} else if (!xmlStrcmp (child->name, RB_PARSE_LIKE)) {
			data->type = RHYTHMDB_QUERY_PROP_LIKE;
		} else if (!xmlStrcmp (child->name, RB_PARSE_NOT_LIKE)) {
			data->type = RHYTHMDB_QUERY_PROP_NOT_LIKE;
		} else if (!xmlStrcmp (child->name, RB_PARSE_PREFIX)) {
			data->type = RHYTHMDB_QUERY_PROP_PREFIX;
		} else if (!xmlStrcmp (child->name, RB_PARSE_SUFFIX)) {
			data->type = RHYTHMDB_QUERY_PROP_SUFFIX;
		} else if (!xmlStrcmp (child->name, RB_PARSE_EQUALS)) {
			xmlChar* prop;

			prop = xmlGetProp(child, RB_PARSE_PROP);
			if (!xmlStrcmp(prop, (xmlChar *)"date"))
				data->type = RHYTHMDB_QUERY_PROP_YEAR_EQUALS;
			else
				data->type = RHYTHMDB_QUERY_PROP_EQUALS;
			xmlFree (prop);
		} else if (!xmlStrcmp (child->name, RB_PARSE_NOT_EQUAL)) {
			xmlChar* prop;

			prop = xmlGetProp(child, RB_PARSE_PROP);
			if (!xmlStrcmp(prop, (xmlChar *)"date"))
				data->type = RHYTHMDB_QUERY_PROP_YEAR_NOT_EQUAL;
			else
				data->type = RHYTHMDB_QUERY_PROP_NOT_EQUAL;
			xmlFree (prop);
		} else if (!xmlStrcmp (child->name, RB_PARSE_GREATER)) {
			xmlChar* prop;

			prop = xmlGetProp(child, RB_PARSE_PROP);
			if (!xmlStrcmp(prop, (xmlChar *)"date"))
				data->type = RHYTHMDB_QUERY_PROP_YEAR_GREATER;
			else
				data->type = RHYTHMDB_QUERY_PROP_GREATER;
			xmlFree (prop);
		} else if (!xmlStrcmp (child->name, RB_PARSE_LESS)) {
			xmlChar* prop;

			prop = xmlGetProp(child, RB_PARSE_PROP);
			if (!xmlStrcmp(prop, (xmlChar *)"date"))
				data->type = RHYTHMDB_QUERY_PROP_YEAR_LESS;
			else
				data->type = RHYTHMDB_QUERY_PROP_LESS;
			xmlFree (prop);
		} else if (!xmlStrcmp (child->name, RB_PARSE_CURRENT_TIME_WITHIN)) {
			data->type = RHYTHMDB_QUERY_PROP_CURRENT_TIME_WITHIN;
		} else if (!xmlStrcmp (child->name, RB_PARSE_CURRENT_TIME_NOT_WITHIN)) {
			data->type = RHYTHMDB_QUERY_PROP_CURRENT_TIME_NOT_WITHIN;
		} else
 			g_assert_not_reached ();

		if (!xmlStrcmp (child->name, RB_PARSE_LIKE)
		    || !xmlStrcmp (child->name, RB_PARSE_NOT_LIKE)
		    || !xmlStrcmp (child->name, RB_PARSE_PREFIX)
		    || !xmlStrcmp (child->name, RB_PARSE_SUFFIX)
		    || !xmlStrcmp (child->name, RB_PARSE_EQUALS)
		    || !xmlStrcmp (child->name, RB_PARSE_NOT_EQUAL)
		    || !xmlStrcmp (child->name, RB_PARSE_GREATER)
		    || !xmlStrcmp (child->name, RB_PARSE_LESS)
		    || !xmlStrcmp (child->name, RB_PARSE_YEAR_EQUALS)
		    || !xmlStrcmp (child->name, RB_PARSE_YEAR_GREATER)
		    || !xmlStrcmp (child->name, RB_PARSE_YEAR_LESS)
		    || !xmlStrcmp (child->name, RB_PARSE_CURRENT_TIME_WITHIN)
		    || !xmlStrcmp (child->name, RB_PARSE_CURRENT_TIME_NOT_WITHIN)) {
			char *content;
			xmlChar *propstr = xmlGetProp (child, RB_PARSE_PROP);
			gint propid = rhythmdb_propid_from_nice_elt_name (db, propstr);
			g_free (propstr);

			g_assert (propid >= 0 && propid < RHYTHMDB_NUM_PROPERTIES);

			data->propid = propid;
			data->val = g_new0 (GValue, 1);

			content = (char *)xmlNodeGetContent (child);
			rhythmdb_read_encoded_property (db, content, data->propid, data->val);
			g_free (content);
		}

		g_ptr_array_add (query, data);
	}

	return query;
}

/*
 * This is used to "process" queries, before using them. It is mainly used to two things:
 *
 * 1) performing expensive data transformations once per query, rather than
 *    once per entry we try to match against. e.g. RHYTHMDB_PROP_SEARCH_MATCH
 *
 * 2) defining criteria in terms of other lower-level ones that the db backend
 *    actually implements. e.g. RHYTHMDB_QUERY_YEAR_*
 */

/**
 * rhythmdb_query_preprocess:
 * @db: the #RhythmDB
 * @query: query to preprocess
 *
 * Preprocesses a query to prepare it for execution.  This has two main
 * roles: to perform expensive data transformations once per query, rather
 * than once per entry, and converting criteria to lower-level forms that
 * are implemented by the database backend.
 *
 * For RHYTHMDB_PROP_SEARCH_MATCH, this converts the search terms into
 * an array of case-folded words.
 *
 * When matching against case-folded properties such as
 * #RHYTHMDB_PROP_TITLE_FOLDED, this case-folds the query value.
 *
 * When performing year-based criteria such as #RHYTHMDB_QUERY_PROP_YEAR_LESS,
 * it converts the year into the Julian date such that a simple numeric
 * comparison will work.
 */
void
rhythmdb_query_preprocess (RhythmDB *db, GPtrArray *query)
{
	int i;

	if (query == NULL)
		return;

	for (i = 0; i < query->len; i++) {
		RhythmDBQueryData *data = g_ptr_array_index (query, i);
		gboolean restart_criteria = FALSE;

		if (data->subquery) {
			rhythmdb_query_preprocess (db, data->subquery);
		} else switch (data->propid) {
			case RHYTHMDB_PROP_TITLE_FOLDED:
			case RHYTHMDB_PROP_GENRE_FOLDED:
			case RHYTHMDB_PROP_ARTIST_FOLDED:
			case RHYTHMDB_PROP_COMPOSER_FOLDED:
			case RHYTHMDB_PROP_ALBUM_FOLDED:
			{
				/* as we are matching against a folded property, the string needs to also be folded */
				const char *orig = g_value_get_string (data->val);
				char *folded = rb_search_fold (orig);

				g_value_reset (data->val);
				g_value_take_string (data->val, folded);
				break;
			}

			case RHYTHMDB_PROP_SEARCH_MATCH:
			{
				const char *orig = g_value_get_string (data->val);
				char *folded = rb_search_fold (orig);
				char **words = rb_string_split_words (folded);

				g_free (folded);
				g_value_unset (data->val);
				g_value_init (data->val, G_TYPE_STRV);
				g_value_take_boxed (data->val, words);
				break;
			}

			case RHYTHMDB_PROP_DATE:
			{
				GDate date = {0,};
				gulong search_date;
				gulong begin;
				gulong end;
				gulong year;

				search_date = g_value_get_ulong (data->val);
				
				/* GDate functions don't handle Year="0", so we need to special case this */
				if (search_date != 0) {
					g_date_set_julian (&date, search_date);
					year = g_date_get_year (&date);
					g_date_clear (&date, 1);

					/* get Julian dates for beginning and end of year */
					g_date_set_dmy (&date, 1, G_DATE_JANUARY, year);
					begin = g_date_get_julian (&date);
					g_date_clear (&date, 1);

					/* and the day before the beginning of the next year */
					g_date_set_dmy (&date, 1, G_DATE_JANUARY, year + 1);
					end =  g_date_get_julian (&date) - 1;
				} else {
					begin = 0;
					end = 0;
				}

				switch (data->type)
				{
				case RHYTHMDB_QUERY_PROP_YEAR_EQUALS:
					restart_criteria = TRUE;
					data->type = RHYTHMDB_QUERY_SUBQUERY;
					data->subquery = rhythmdb_query_parse (db,
									       RHYTHMDB_QUERY_PROP_GREATER, data->propid, begin,
									       RHYTHMDB_QUERY_PROP_LESS, data->propid, end,
									       RHYTHMDB_QUERY_END);
					break;
				case RHYTHMDB_QUERY_PROP_YEAR_NOT_EQUAL:
					restart_criteria = TRUE;
					data->type = RHYTHMDB_QUERY_SUBQUERY;
					data->subquery = rhythmdb_query_parse (db,
									       RHYTHMDB_QUERY_PROP_LESS, data->propid, begin-1,
									       RHYTHMDB_QUERY_DISJUNCTION,
									       RHYTHMDB_QUERY_PROP_GREATER, data->propid, end+1,
									       RHYTHMDB_QUERY_END);
					break;

				case RHYTHMDB_QUERY_PROP_YEAR_LESS:
					restart_criteria = TRUE;
					data->type = RHYTHMDB_QUERY_PROP_LESS;
					g_value_set_ulong (data->val, end);
					break;

				case RHYTHMDB_QUERY_PROP_YEAR_GREATER:
					restart_criteria = TRUE;
					data->type = RHYTHMDB_QUERY_PROP_GREATER;
					g_value_set_ulong (data->val, begin);
					break;

				default:
					break;
				}

				break;
			}

			default:
				break;
		}

		/* re-do this criteria, in case it needs further transformation */
		if (restart_criteria)
			i--;
	}
}

/**
 * rhythmdb_query_append_prop_multiple:
 * @db: the #RhythmDB
 * @query: the query to append to
 * @propid: property ID to match
 * @items: (element-type GObject.Value): #GList of values to match against
 *
 * Appends a set of criteria to a query to match against any of the values
 * listed in @items.
 */
void
rhythmdb_query_append_prop_multiple (RhythmDB *db, GPtrArray *query, RhythmDBPropType propid, GList *items)
{
	GPtrArray *subquery;

	if (items == NULL)
		return;

	if (items->next == NULL) {
		rhythmdb_query_append (db,
				       query,
				       RHYTHMDB_QUERY_PROP_EQUALS,
				       propid,
				       items->data,
				       RHYTHMDB_QUERY_END);
		return;
	}

	subquery = g_ptr_array_new ();

	rhythmdb_query_append (db,
			       subquery,
			       RHYTHMDB_QUERY_PROP_EQUALS,
			       propid,
			       items->data,
			       RHYTHMDB_QUERY_END);
	items = items->next;
	while (items) {
		rhythmdb_query_append (db,
				       subquery,
				       RHYTHMDB_QUERY_DISJUNCTION,
				       RHYTHMDB_QUERY_PROP_EQUALS,
				       propid,
				       items->data,
				       RHYTHMDB_QUERY_END);
		items = items->next;
	}
	rhythmdb_query_append (db, query, RHYTHMDB_QUERY_SUBQUERY, subquery,
			       RHYTHMDB_QUERY_END);
}

/**
 * rhythmdb_query_is_time_relative:
 * @db: the #RhythmDB
 * @query: the query to check
 *
 * Checks if a query contains any time-relative criteria.
 *
 * Return value: %TRUE if time-relative criteria found
 */
gboolean
rhythmdb_query_is_time_relative (RhythmDB *db, GPtrArray *query)
{
	int i;
	if (query == NULL)
		return FALSE;

	for (i=0; i < query->len; i++) {
		RhythmDBQueryData *data = g_ptr_array_index (query, i);

		if (data->subquery) {
			if (rhythmdb_query_is_time_relative (db, data->subquery))
				return TRUE;
			else
				continue;
		}

		switch (data->type) {
		case RHYTHMDB_QUERY_PROP_CURRENT_TIME_WITHIN:
		case RHYTHMDB_QUERY_PROP_CURRENT_TIME_NOT_WITHIN:
			return TRUE;
		default:
			break;
		}
	}

	return FALSE;
}

/**
 * rhythmdb_query_to_string:
 * @db: a #RhythmDB instance
 * @query: a query.
 *
 * Returns a supposedly human-readable form of the query.
 * This is only intended for debug usage.
 *
 * Returns: allocated string form of the query
 **/
char *
rhythmdb_query_to_string (RhythmDB *db, GPtrArray *query)
{
	GString *buf;
	int i;

	buf = g_string_sized_new (100);
	for (i = 0; i < query->len; i++) {
		char *fmt = NULL;
		RhythmDBQueryData *data = g_ptr_array_index (query, i);

		switch (data->type) {
		case RHYTHMDB_QUERY_SUBQUERY:
			{
				char *s;

				s = rhythmdb_query_to_string (db, data->subquery);
				g_string_append_printf (buf, "{ %s }",  s);
				g_free (s);
			}
			break;
		case RHYTHMDB_QUERY_PROP_LIKE:
			fmt = "(%s =~ %s)";
			break;
		case RHYTHMDB_QUERY_PROP_NOT_LIKE:
			fmt = "(%s !~ %s)";
			break;
		case RHYTHMDB_QUERY_PROP_PREFIX:
			fmt = "(%s |< %s)";
			break;
		case RHYTHMDB_QUERY_PROP_SUFFIX:
			fmt = "(%s >| %s)";
			break;
		case RHYTHMDB_QUERY_PROP_EQUALS:
			fmt = "(%s == %s)";
			break;
		case RHYTHMDB_QUERY_PROP_NOT_EQUAL:
			fmt = "(%s != %s)";
			break;
		case RHYTHMDB_QUERY_PROP_YEAR_EQUALS:
			fmt = "(year(%s) == %s)";
			break;
		case RHYTHMDB_QUERY_PROP_YEAR_NOT_EQUAL:
			fmt = "(year(%s) != %s)";
			break;
		case RHYTHMDB_QUERY_DISJUNCTION:
			g_string_append_printf (buf, " || ");
			break;
		case RHYTHMDB_QUERY_END:
			break;
		case RHYTHMDB_QUERY_PROP_GREATER:
			fmt = "(%s > %s)";
			break;
		case RHYTHMDB_QUERY_PROP_YEAR_GREATER:
			fmt = "(year(%s) > %s)";
			break;
		case RHYTHMDB_QUERY_PROP_LESS:
			fmt = "(%s < %s)";
			break;
		case RHYTHMDB_QUERY_PROP_YEAR_LESS:
			fmt = "(year(%s) < %s)";
			break;
		case RHYTHMDB_QUERY_PROP_CURRENT_TIME_WITHIN:
			fmt = "(%s <> %s)";
			break;
		case RHYTHMDB_QUERY_PROP_CURRENT_TIME_NOT_WITHIN:
			fmt = "(%s >< %s)";
			break;
		}

		if (fmt) {
			char *value;

			value = prop_gvalue_to_string (db, data->propid, data->val);
			g_string_append_printf (buf, fmt,
						rhythmdb_nice_elt_name_from_propid (db, data->propid),
						value);
			g_free (value);
			fmt = NULL;
		}
	}

	return g_string_free (buf, FALSE);
}

GType
rhythmdb_query_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		type = g_boxed_type_register_static ("RhythmDBQuery",
						     (GBoxedCopyFunc)rhythmdb_query_copy,
						     (GBoxedFreeFunc)rhythmdb_query_free);
	}

	return type;
}
