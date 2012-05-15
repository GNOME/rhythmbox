/*
 *  Copyright (C) 2006 Jonathan Matthew <jonathan@kaolin.hn.org>
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

/*
 * Common junk for the out-of-process metadata reader.
 */

#include <config.h>
#include <glib.h>

#include "rb-metadata.h"
#include "rb-metadata-dbus.h"
#include "rb-debug.h"

const char *rb_metadata_iface_xml =
"<node>								"
"  <interface name='org.gnome.Rhythmbox3.Metadata'>		"
"    <method name='ping'>					"
"      <arg direction='out' type='b' name='ok'/>		"
"    </method>							"
"    <method name='load'>					"
"      <arg direction='in' type='s' name='uri'/>		"
"      <arg direction='out' type='as' name='missingPlugins'/>	"
"      <arg direction='out' type='as' name='pluginDescriptions'/> "
"      <arg direction='out' type='b' name='hasAudio'/>		"
"      <arg direction='out' type='b' name='hasVideo'/>		"
"      <arg direction='out' type='b' name='hasOtherData'/>	"
"      <arg direction='out' type='s' name='mediaType'/>		"
"      <arg direction='out' type='b' name='ok'/>		"
"      <arg direction='out' type='i' name='errorCode'/>		"
"      <arg direction='out' type='s' name='errorString'/>	"
"      <arg direction='out' type='a{iv}' name='metadata'/>	"
"    </method>							"
"    <method name='getSaveableTypes'>				"
"      <arg direction='out' type='as' name='types'/>		"
"    </method>							"
"    <method name='save'>					"
"      <arg direction='in' type='s' name='uri'/>		"
"      <arg direction='in' type='a{iv}' name='metadata'/>	"
"      <arg direction='out' type='b' name='ok'/>		"
"      <arg direction='out' type='i' name='errorCode'/>		"
"      <arg direction='out' type='s' name='errorString'/>	"
"    </method>							"
"  </interface>							"
"</node>";

GVariantBuilder *
rb_metadata_dbus_get_variant_builder (RBMetaData *md)
{
	GVariantBuilder *b;
	RBMetaDataField field;
	int count = 0;

	b = g_variant_builder_new (G_VARIANT_TYPE_ARRAY);
	for (field = RB_METADATA_FIELD_TITLE; field < RB_METADATA_FIELD_LAST; field++) {
		GValue v = {0,};
		GVariant *value;

		if (!rb_metadata_get (md, field, &v))
			continue;

		if (G_VALUE_HOLDS_STRING (&v)) {
			value = g_variant_new_string (g_value_get_string (&v));
		} else if (G_VALUE_HOLDS_ULONG (&v)) {
			value = g_variant_new_uint32 (g_value_get_ulong (&v));
		} else if (G_VALUE_HOLDS_DOUBLE (&v)) {
			value = g_variant_new_double (g_value_get_double (&v));
		} else {
			g_assert_not_reached ();
		}
		g_value_unset (&v);

		g_variant_builder_add (b, "{iv}", field, value);
		count++;
	}

	/* make sure there's at least one entry in the map so we can
	 * build the response message.
	 */
	if (count == 0) {
		g_variant_builder_add (b, "{iv}", RB_METADATA_FIELD_TRACK_NUMBER, g_variant_new_uint32 (0));
	}

	return b;
}
