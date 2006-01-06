/*
 *  arch-tag: Implementation of RhythmDB query creation properties
 *
 *  Copyright (C) 2003, 2004 Colin Walters <walters@gnome.org>
 *  Copyright (C) 2005 James Livingston <walters@gnome.org>
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

#include <libgnome/gnome-i18n.h>
#include <gtk/gtk.h>

#include "rhythmdb.h"
#include "rb-query-creator-private.h"
#include "rb-rating.h"

const RBQueryCreatorPropertyType string_property_type;
const RBQueryCreatorPropertyType escaped_string_property_type;
const RBQueryCreatorPropertyType rating_property_type;
const RBQueryCreatorPropertyType integer_property_type;
const RBQueryCreatorPropertyType year_property_type;
const RBQueryCreatorPropertyType duration_property_type;
const RBQueryCreatorPropertyType relative_time_property_type;

static GtkWidget * stringCriteriaCreateWidget (gboolean *constrain);
static void stringCriteriaSetWidgetData (GtkWidget *widget, GValue *val);
static void stringCriteriaGetWidgetData (GtkWidget *widget, GValue *val);
static void escapedStringCriteriaSetWidgetData (GtkWidget *widget, GValue *val);
static void escapedStringCriteriaGetWidgetData (GtkWidget *widget, GValue *val);
static GtkWidget * ratingCriteriaCreateWidget (gboolean *constrain);
static void ratingCriteriaSetWidgetData (GtkWidget *widget, GValue *val);
static void ratingCriteriaGetWidgetData (GtkWidget *widget, GValue *val);
static GtkWidget * integerCriteriaCreateWidget (gboolean *constrain);
static void integerCriteriaSetWidgetData (GtkWidget *widget, GValue *val);
static void integerCriteriaGetWidgetData (GtkWidget *widget, GValue *val);
static GtkWidget * yearCriteriaCreateWidget (gboolean *constrain);
static void yearCriteriaSetWidgetData (GtkWidget *widget, GValue *val);
static void yearCriteriaGetWidgetData (GtkWidget *widget, GValue *val);
static GtkWidget * durationCriteriaCreateWidget (gboolean *constrain);
static void durationCriteriaSetWidgetData (GtkWidget *widget, GValue *val);
static void durationCriteriaGetWidgetData (GtkWidget *widget, GValue *val);
static GtkWidget * relativeTimeCriteriaCreateWidget (gboolean *constrain);
static void relativeTimeCriteriaSetWidgetData (GtkWidget *widget, GValue *val);
static void relativeTimeCriteriaGetWidgetData (GtkWidget *widget, GValue *val);


/*
 * This table is the list of properties that are displayed in the query-creator
 */
const RBQueryCreatorPropertyOption property_options[] =
{
	{ N_("Title"), RHYTHMDB_PROP_TITLE, RHYTHMDB_PROP_TITLE_FOLDED, &string_property_type },
	{ N_("Artist"), RHYTHMDB_PROP_ARTIST, RHYTHMDB_PROP_ARTIST_FOLDED, &string_property_type },
	{ N_("Album"), RHYTHMDB_PROP_ALBUM, RHYTHMDB_PROP_ALBUM_FOLDED, &string_property_type },
	{ N_("Genre"), RHYTHMDB_PROP_GENRE, RHYTHMDB_PROP_GENRE_FOLDED, &string_property_type },
	{ N_("Year"), RHYTHMDB_PROP_DATE, RHYTHMDB_PROP_DATE, &year_property_type },
	{ N_("Rating"), RHYTHMDB_PROP_RATING, RHYTHMDB_PROP_RATING, &rating_property_type },
	{ N_("Path"), RHYTHMDB_PROP_LOCATION, RHYTHMDB_PROP_LOCATION, &escaped_string_property_type },

	{ N_("Play Count"), RHYTHMDB_PROP_PLAY_COUNT, RHYTHMDB_PROP_PLAY_COUNT, &integer_property_type },
	{ N_("Track Number"), RHYTHMDB_PROP_TRACK_NUMBER, RHYTHMDB_PROP_TRACK_NUMBER, &integer_property_type },
	{ N_("Disc Number"), RHYTHMDB_PROP_DISC_NUMBER, RHYTHMDB_PROP_DISC_NUMBER, &integer_property_type },
	{ N_("Bitrate"), RHYTHMDB_PROP_BITRATE, RHYTHMDB_PROP_BITRATE, &integer_property_type },

	{ N_("Duration"), RHYTHMDB_PROP_DURATION, RHYTHMDB_PROP_DURATION, &duration_property_type },

	{ N_("Time of Last Play"), RHYTHMDB_PROP_LAST_PLAYED, RHYTHMDB_PROP_LAST_PLAYED, &relative_time_property_type },
	{ N_("Time Added to Library"), RHYTHMDB_PROP_FIRST_SEEN, RHYTHMDB_PROP_FIRST_SEEN, &relative_time_property_type },
};

const int num_property_options = G_N_ELEMENTS (property_options);


/*
 * This table describes which properties can be used for sorting a playlist
 * All entries MUST have column keys column keys listed in rb-entry-view.c
 */
const RBQueryCreatorSortOption sort_options[] =
{
	{ N_("Artist"), "Artist", N_("_In reverse alphabetical order") },
	{ N_("Album"), "Album", N_("_In reverse alphabetical order") },
	{ N_("Genre"), "Genre", N_("_In reverse alphabetical order") },
	{ N_("Title"), "Title", N_("_In reverse alphabetical order") },
	{ N_("Rating"), "Rating", N_("W_ith more highly rated tracks first") },
	{ N_("Play Count"), "PlayCount", N_("W_ith more often played songs first") },
	{ N_("Year"), "Year", N_("W_ith newer tracks first") },
	{ N_("Duration"), "Time", N_("W_ith longer tracks first") },
	{ N_("Track Number"), "Track", N_("_In decreasing order")},
	{ N_("Last Played"), "LastPlayed", N_("W_ith more recently played tracks first") },
	{ N_("Date Added"), "FirstSeen", N_("W_ith more recently added tracks first") },
};

const int num_sort_options = G_N_ELEMENTS (sort_options);
const int DEFAULT_SORTING_COLUMN = 0;
const gint DEFAULT_SORTING_ORDER = GTK_SORT_ASCENDING;


/*
 * This is the property type for string properties
 */

const RBQueryCreatorCriteriaOption string_criteria_options[] =
{
	{ N_("contains"), 0, RHYTHMDB_QUERY_PROP_LIKE },
	{ N_("does not contain"), 0, RHYTHMDB_QUERY_PROP_NOT_LIKE },
	{ N_("equals"), 1, RHYTHMDB_QUERY_PROP_EQUALS }
};

const RBQueryCreatorPropertyType string_property_type =
{
	G_N_ELEMENTS (string_criteria_options),
	string_criteria_options,
	stringCriteriaCreateWidget,
	stringCriteriaSetWidgetData,
	stringCriteriaGetWidgetData
};

const RBQueryCreatorPropertyType escaped_string_property_type =
{
	G_N_ELEMENTS (string_criteria_options),
	string_criteria_options,
	stringCriteriaCreateWidget,
	escapedStringCriteriaSetWidgetData,
	escapedStringCriteriaGetWidgetData
};


/*
 * This are the property types for numeric quantities, such as rating and playcounts
 */

const RBQueryCreatorCriteriaOption numeric_criteria_options[] =
{
	{ N_("equals"), 1, RHYTHMDB_QUERY_PROP_EQUALS },
	{ N_("at least"), 1, RHYTHMDB_QUERY_PROP_GREATER },	/* matches if A >= B */
	{ N_("at most"), 1, RHYTHMDB_QUERY_PROP_LESS }		/* matches if A <= B */
};

/*
 * Property type for date quantities 
 */

const RBQueryCreatorCriteriaOption year_criteria_options[] =
{
	{ N_("in"), 1, RHYTHMDB_QUERY_PROP_YEAR_EQUALS }, 
	/* matches if within 1-JAN-YEAR to 31-DEC-YEAR */
	{ N_("after"), 1, RHYTHMDB_QUERY_PROP_YEAR_GREATER },	
	/* matches if >= 31-DEC-YEAR */
	{ N_("before"), 1, RHYTHMDB_QUERY_PROP_YEAR_LESS }		
	/* matches if < 1-DEC-YEAR */
};

const RBQueryCreatorPropertyType rating_property_type =
{
	G_N_ELEMENTS (numeric_criteria_options),
	numeric_criteria_options,
	ratingCriteriaCreateWidget,
	ratingCriteriaSetWidgetData,
	ratingCriteriaGetWidgetData
};

const RBQueryCreatorPropertyType integer_property_type =
{
	G_N_ELEMENTS (numeric_criteria_options),
	numeric_criteria_options,
	integerCriteriaCreateWidget,
	integerCriteriaSetWidgetData,
	integerCriteriaGetWidgetData
};

const RBQueryCreatorPropertyType year_property_type =
{
	G_N_ELEMENTS (year_criteria_options),
	year_criteria_options,
	yearCriteriaCreateWidget,
	yearCriteriaSetWidgetData,
	yearCriteriaGetWidgetData
};

const RBQueryCreatorPropertyType duration_property_type =
{
	G_N_ELEMENTS (numeric_criteria_options),
	numeric_criteria_options,
	durationCriteriaCreateWidget,
	durationCriteriaSetWidgetData,
	durationCriteriaGetWidgetData
};


/*
 * This is the property type for relative time properties, such as last played and first seen
 */

typedef struct
{
	const char *name;
	gulong timeMultiplier;
} RBQueryCreatorTimeUnitOption;

const RBQueryCreatorCriteriaOption relative_time_criteria_options[] =
{
	/*
	 * Translators: this will match when within <value> of the current time
	 * e.g. "in the last" "7 days" will match if within 7 days of the current time
	 */
	{ N_("in the last"), 1, RHYTHMDB_QUERY_PROP_CURRENT_TIME_WITHIN },
	
	/*
	 * Translators: this is the opposite of the above, and will match if not
	 * within <value> of the current time
	 */
	{ N_("not in the last"), 1, RHYTHMDB_QUERY_PROP_CURRENT_TIME_NOT_WITHIN }
};

const RBQueryCreatorPropertyType relative_time_property_type =
{
	G_N_ELEMENTS (relative_time_criteria_options),
	relative_time_criteria_options,
	relativeTimeCriteriaCreateWidget,
	relativeTimeCriteriaSetWidgetData,
	relativeTimeCriteriaGetWidgetData
};

const RBQueryCreatorTimeUnitOption time_unit_options[] =
{
	{ N_("seconds"), 1 },
	{ N_("minutes"), 60 },
	{ N_("hours"), 60 * 60 },
	{ N_("days"), 60 * 60 * 24 },
	{ N_("weeks"), 60 * 60 * 24 * 7 }
};

const int time_unit_options_default = 4; /* days */


/*
 * Implementation for the string properties, using a single GtkEntry.
 */

static GtkWidget *
stringCriteriaCreateWidget (gboolean *constrain)
{
	return gtk_entry_new ();
}

static void
stringCriteriaSetWidgetData (GtkWidget *widget, GValue *val)
{
	gtk_entry_set_text (GTK_ENTRY (widget), g_value_get_string (val));
}

static void
stringCriteriaGetWidgetData (GtkWidget *widget, GValue *val)
{
	const char* text = gtk_entry_get_text (GTK_ENTRY (widget));

	g_value_init (val, G_TYPE_STRING);
	g_value_set_string (val, text);
}

/* escaped string operations, for use with URIs, etc */

static void
escapedStringCriteriaSetWidgetData (GtkWidget *widget, GValue *val)
{
	char *text = gnome_vfs_unescape_string (g_value_get_string (val), NULL);
	gtk_entry_set_text (GTK_ENTRY (widget), text);
	g_free (text);
}

static void
escapedStringCriteriaGetWidgetData (GtkWidget *widget, GValue *val)
{
	char *text = gnome_vfs_escape_path_string (gtk_entry_get_text (GTK_ENTRY (widget)));

	g_value_init (val, G_TYPE_STRING);
	g_value_set_string (val, text);
}


/*
 * Implementation for the ratings property, using the RbRating widget
 */

static void
set_rating_score (RBRating *rating, gdouble score)
{
	g_object_set (G_OBJECT (rating), "rating", score, NULL);
}

static GtkWidget *
ratingCriteriaCreateWidget (gboolean *constrain)
{
	RBRating *rating = rb_rating_new ();
	g_signal_connect_object (G_OBJECT (rating), "rated",
				 G_CALLBACK (set_rating_score), NULL, 0);
	*constrain = FALSE;
	return GTK_WIDGET (rating);
}

static void
ratingCriteriaSetWidgetData (GtkWidget *widget, GValue *val)
{
	g_object_set (G_OBJECT (widget), "rating", g_value_get_double (val), NULL);
}

static void
ratingCriteriaGetWidgetData (GtkWidget *widget, GValue *val)
{
	double rating;
	g_object_get (G_OBJECT (widget), "rating", &rating, NULL);

	g_value_init (val, G_TYPE_DOUBLE);
	g_value_set_double (val, rating);
}


/*
 * Implementation for the integer properties, using a single GtkSpinButton.
 */

static GtkWidget *
integerCriteriaCreateWidget (gboolean *constrain)
{
	return gtk_spin_button_new_with_range (0.0, (double)G_MAXINT, 1.0);
}

static void
integerCriteriaSetWidgetData (GtkWidget *widget, GValue *val)
{
	gulong num = g_value_get_ulong (val);
	g_assert (num <= G_MAXINT);
		
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (widget), (gint)num );
}

static void
integerCriteriaGetWidgetData (GtkWidget *widget, GValue *val)
{
	gint num = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (widget));
	g_assert (num >= 0);

	g_value_init (val, G_TYPE_ULONG);
	g_value_set_ulong (val, (gulong)num);
}

/* Implementation for Year properties, using a single GtkSpinButton. */

static GtkWidget *
yearCriteriaCreateWidget (gboolean *constrain)
{
	return gtk_spin_button_new_with_range (0.0, (double)G_MAXINT, 1.0);
}

static void
yearCriteriaSetWidgetData (GtkWidget *widget, GValue *val)
{
	GDate *date = NULL;
	gulong num = g_value_get_ulong (val);
	g_assert (num <= G_MAXINT);

	/* Create a date structure to get year from */
	date = g_date_new();
	g_date_set_julian (date, num);
	
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (widget), (gint)g_date_get_year(date));
	g_date_free(date);
}

static void
yearCriteriaGetWidgetData (GtkWidget *widget, GValue *val)
{
	GDate *date = NULL;
	gint num = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (widget));
	g_assert (num >=  0);

	/* New date structure, use year set in widget */
	date = g_date_new_dmy (1, G_DATE_JANUARY, num);

	g_value_init (val, G_TYPE_ULONG);
	g_value_set_ulong (val, (gulong) g_date_get_julian (date) );
	g_date_free(date);
}



/*
 * Implementation for the duration property, using two single GtkSpinButtons.
 */

static GtkWidget *
durationCriteriaCreateWidget (gboolean *constrain)
{
	GtkBox *box;
	GtkWidget *minutesSpin;
	GtkWidget *minutesLabel;
	GtkWidget *secondsSpin;

	/* the widget for Duration is set out like the following [ 2] : [30] */
	box = GTK_BOX (gtk_hbox_new (FALSE, 3));
	
	minutesSpin = gtk_spin_button_new_with_range (0.0, G_MAXINT, 1.0);
	gtk_box_pack_start (box, minutesSpin, FALSE, FALSE, 0);
	
	minutesLabel = gtk_label_new (":");
	gtk_box_pack_start (box, minutesLabel, FALSE, FALSE, 0);
	
	secondsSpin = gtk_spin_button_new_with_range (0.0, 59.0, 1.0);
	gtk_box_pack_start (box, secondsSpin, FALSE, FALSE, 0);
	
	gtk_widget_show_all (GTK_WIDGET (box));
	return GTK_WIDGET (box);
}

static void
durationCriteriaSetWidgetData (GtkWidget *widget, GValue *val)
{
	GtkSpinButton *minutesSpinner = GTK_SPIN_BUTTON (get_box_widget_at_pos (GTK_BOX (widget), 0));
	GtkSpinButton *secondsSpinner = GTK_SPIN_BUTTON (get_box_widget_at_pos (GTK_BOX (widget), 2));
	
	gtk_spin_button_set_value (minutesSpinner, (gdouble) (g_value_get_ulong (val) / 60));
	gtk_spin_button_set_value (secondsSpinner, (gdouble) (g_value_get_ulong (val) % 60));
}

static void
durationCriteriaGetWidgetData (GtkWidget *widget, GValue *val)
{
	
	GtkSpinButton *minutesSpinner = GTK_SPIN_BUTTON (get_box_widget_at_pos (GTK_BOX (widget), 0));
	GtkSpinButton *secondsSpinner = GTK_SPIN_BUTTON (get_box_widget_at_pos (GTK_BOX (widget), 2));
	
	gint value = gtk_spin_button_get_value_as_int (minutesSpinner) * 60
		   + gtk_spin_button_get_value_as_int (secondsSpinner);
	g_assert (value >= 0);

	g_value_init (val, G_TYPE_ULONG);
	g_value_set_ulong (val, (gulong) value);
}


/*
 * Implementation for the relative time properties, using a spin button and a menu.
 */

static GtkWidget*
create_time_unit_option_menu (const RBQueryCreatorTimeUnitOption *options,
			     int length)
{
	GtkWidget *menu = gtk_menu_new ();
	GtkWidget *option_menu = gtk_option_menu_new ();
	int i;

	for (i = 0; i < length; i++) {
		GtkWidget *menu_item = gtk_menu_item_new_with_label (_(options[i].name));
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
	}

	gtk_widget_show_all (menu);
	gtk_option_menu_set_menu (GTK_OPTION_MENU (option_menu), menu);
  
	return option_menu;
}

static GtkWidget *
relativeTimeCriteriaCreateWidget (gboolean *constrain)
{
	GtkBox *box;

	GtkWidget *timeSpin;
	GtkWidget *timeOption;

	box = GTK_BOX (gtk_hbox_new (FALSE, 6));
	
	timeSpin = gtk_spin_button_new_with_range (1.0, G_MAXINT, 1.0);
	gtk_box_pack_start_defaults (box, timeSpin);
	
	timeOption = create_time_unit_option_menu (time_unit_options, G_N_ELEMENTS (time_unit_options));
	gtk_option_menu_set_history(GTK_OPTION_MENU (timeOption), time_unit_options_default);
	gtk_box_pack_start_defaults (box, timeOption);
		
	gtk_widget_show_all (GTK_WIDGET (box));
	return GTK_WIDGET (box);
}

static void
relativeTimeCriteriaSetWidgetData (GtkWidget *widget, GValue *val)
{
	GtkBox *box = GTK_BOX (widget);
	
	GtkSpinButton *timeSpin = GTK_SPIN_BUTTON (get_box_widget_at_pos (box, 0));
	GtkOptionMenu *unitMenu = GTK_OPTION_MENU (get_box_widget_at_pos (box, 1));
	
	gulong time = g_value_get_ulong (val);
	gulong unit = 0;
	int i;

	/* determine the best units to use for the given value */
	for (i = 0; i < G_N_ELEMENTS(time_unit_options); i++) {
		/* find out if the time is an even multiple of the unit */
		if (time % time_unit_options[i].timeMultiplier == 0)
			unit = i;
	}

	time = time / time_unit_options[unit].timeMultiplier;
	g_assert (time < G_MAXINT);
	/* set the time value and unit*/
	gtk_option_menu_set_history(unitMenu, unit);
	gtk_spin_button_set_value(timeSpin, time);
}

static void
relativeTimeCriteriaGetWidgetData (GtkWidget *widget, GValue *val)
{
	GtkSpinButton *timeSpin = GTK_SPIN_BUTTON (get_box_widget_at_pos (GTK_BOX (widget), 0));
	GtkOptionMenu *unitMenu = GTK_OPTION_MENU (get_box_widget_at_pos (GTK_BOX (widget), 1));

	gulong timeMultiplier = time_unit_options [gtk_option_menu_get_history (unitMenu)].timeMultiplier;
	gint value = gtk_spin_button_get_value_as_int (timeSpin) * timeMultiplier;
	g_assert (value >= 0);

	g_value_init (val, G_TYPE_ULONG);
	g_value_set_ulong (val, (gulong) value);
}
