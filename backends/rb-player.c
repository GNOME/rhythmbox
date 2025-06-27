/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2006  James Livingston  <doclivingston@gmail.com>
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
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <config.h>

#include "rb-player.h"
#include "rb-player-gst.h"
#include "rb-player-gst-multi.h"
#include "rb-util.h"

/**
 * RBPlayerPlayType:
 * @RB_PLAYER_PLAY_REPLACE: Replace the existing stream
 * @RB_PLAYER_PLAY_AFTER_EOS: Start the new stream after the current stream ends
 * @RB_PLAYER_PLAY_CROSSFADE: Crossfade between the existing stream and the new stream
 */

/**
 * RBPlayerError:
 * @RB_PLAYER_ERROR_NO_AUDIO: Audio playback not available
 * @RB_PLAYER_ERROR_GENERAL: Nonspecific error
 * @RB_PLAYER_ERROR_INTERNAL: Internal error
 * @RB_PLAYER_ERROR_NOT_FOUND: The resource could not be found
 */

/* Signals */
enum {
	EOS,
	INFO,
	BUFFERING,
	ERROR,
	TICK,
	EVENT,
	PLAYING_STREAM,
	VOLUME_CHANGED,
	IMAGE,
	REDIRECT,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

/**
 * SECTION:rbplayer
 * @short_description: playback backend interface
 * @include: rb-player.h
 *
 * This is the interface implemented by the rhythmbox playback backends.
 * It allows the caller to control playback (open, play, pause, close), 
 * seek (set_time), control volume (get_volume, set_volume)
 * and receive playback state information (get_time, various signals).
 *
 * The playback interface allows for multiple streams to be playing (or at
 * least open) concurrently. The caller associates some data with each stream
 * it opens (#rb_player_open), which is included in the paramters with each
 * signal emitted. The caller should not assume that the new stream is playing
 * immediately upon returning from #rb_player_play. Instead, it should use
 * the 'playing-stream' signal to determine that.
 *
 * The player implementation should emit signals for metadata extracted from the
 * stream using the 'info' signal
 *
 * While playing, the player implementation should emit 'tick' signals frequently
 * enough to update an elapsed/remaining time display consistently.  The duration
 * value included in tick signal emissions is used to prepare the next stream before
 * the current stream reaches EOS, so it should be updated for each emission to account
 * for variable bitrate streams that produce inaccurate duration estimates early on.
 *
 * When playing a stream from the network, the player can report buffering status
 * using the 'buffering' signal.  The value included in the signal indicates the
 * percentage of the buffer that has been filled.
 *
 * The 'event' signal can be used to communicate events from the player to the application.
 * For GStreamer-based player implementations, events are triggered by elements in the
 * pipeline posting application messages.  The name of the message becomes the name of the
 * event.
 */

static void
rb_player_interface_init (RBPlayerIface *iface)
{
	/**
	 * RBPlayer::eos:
	 * @player: the #RBPlayer
	 * @stream_data: the data associated with the stream that finished
	 * @early: if %TRUE, the EOS notification should only be used for track changes.
	 *
	 * The 'eos' signal is emitted when a stream finishes, or in some cases, when it
	 * is about to finish (with @early set to %TRUE) to allow for a new track to be
	 * played immediately afterwards.
	 **/
	signals[EOS] =
		g_signal_new ("eos",
			      G_TYPE_FROM_INTERFACE (iface),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE,
			      G_STRUCT_OFFSET (RBPlayerIface, eos),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      2, G_TYPE_POINTER, G_TYPE_BOOLEAN);

	/**
	 * RBPlayer::info:
	 * @player: the #RBPlayer
	 * @stream_data: the data associated with the stream
	 * @field: the #RBMetaDataField corresponding to the stream info
	 * @value: the value of the stream info field
	 *
	 * The 'info' signal is emitted when a metadata value is found in
	 * the stream.
	 **/
	signals[INFO] =
		g_signal_new ("info",
			      G_TYPE_FROM_INTERFACE (iface),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlayerIface, info),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      3, G_TYPE_POINTER, G_TYPE_INT, G_TYPE_VALUE);

	/**
	 * RBPlayer::error:
	 * @player: the #RBPlayer
	 * @stream_data: the data associated with the stream
	 * @error: description of the error
	 *
	 * The 'error' signal is emitted when an error is encountered
	 * while opening or playing a stream.
	 **/
	signals[ERROR] =
		g_signal_new ("error",
			      G_TYPE_FROM_INTERFACE (iface),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE,
			      G_STRUCT_OFFSET (RBPlayerIface, error),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      2,
			      G_TYPE_POINTER, G_TYPE_POINTER);

	/**
	 * RBPlayer::tick:
	 * @player: the #RBPlayer
	 * @stream_data: the data associated with the stream
	 * @elapsed: playback position in the stream (in nanoseconds)
	 * @duration: current estimate of the duration of the stream
	 *  (in nanoseconds)
	 *
	 * The 'tick' signal is emitted repeatedly while the stream is
	 * playing. Signal handlers can use this to update UI and to
	 * prepare new streams for crossfade or gapless playback.
	 **/
	signals[TICK] =
		g_signal_new ("tick",
			      G_TYPE_FROM_INTERFACE (iface),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlayerIface, tick),
			      NULL, NULL, NULL,
			      G_TYPE_NONE,
			      3,
			      G_TYPE_POINTER, G_TYPE_INT64, G_TYPE_INT64);

	/**
	 * RBPlayer::buffering:
	 * @player: the #RBPlayer
	 * @stream_data: the data associated with the buffering stream
	 * @progress: buffering percentage
	 *
	 * The 'buffering' signal is emitted while a stream is paused so
	 * that a buffer can be filled.  The progress value typically varies
	 * from 0 to 100, and once it reaches 100, playback resumes.
	 **/
	signals[BUFFERING] =
		g_signal_new ("buffering",
			      G_TYPE_FROM_INTERFACE (iface),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlayerIface, buffering),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      2,
			      G_TYPE_POINTER, G_TYPE_UINT);

	/**
	 * RBPlayer::event:
	 * @player: the #RBPlayer
	 * @stream_data: data associated with the stream
	 * @data: event data
	 *
	 * The 'event' signal provides a means for custom GStreamer
	 * elements to communicate events back to the rest of the
	 * application.  The GStreamer element posts an application
	 * message on the GStreamer bus, which is translated into an
	 * event signal with the detail of the signal set to the name
	 * of the structure found in the message.
	 */
	signals[EVENT] =
		g_signal_new ("event",
			      G_TYPE_FROM_INTERFACE (iface),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
			      G_STRUCT_OFFSET (RBPlayerIface, event),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      2,
			      G_TYPE_POINTER, G_TYPE_POINTER);

	/**
	 * RBPlayer::playing-stream:
	 * @player: the #RBPlayer
	 * @stream_data: data associated with the stream
	 *
	 * The 'playing-stream' signal is emitted when the main playing stream
	 * changes. It should be used to update the UI to show the new
	 * stream. It can either be emitted before or after #rb_player_play returns,
	 * depending on the player backend.
	 */
	signals[PLAYING_STREAM] =
		g_signal_new ("playing-stream",
			      G_TYPE_FROM_INTERFACE (iface),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlayerIface, playing_stream),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_POINTER);
	/**
	 * RBPlayer::volume-changed:
	 * @player: the #RBPlayer
	 * @volume: the new volume level
	 *
	 * The 'volume-changed' signal is emitted when the output stream volume is
	 * changed externally.
	 */
	signals[VOLUME_CHANGED] =
		g_signal_new ("volume-changed",
			      G_TYPE_FROM_INTERFACE (iface),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlayerIface, volume_changed),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_FLOAT);
	
	/**
	 * RBPlayer::image:
	 * @player: the #RBPlayer
	 * @stream_data: data associated with the stream
	 * @image: the image extracted from the stream
	 *
	 * The 'image' signal is emitted to provide access to images extracted
	 * from the stream.
	 */
	signals[IMAGE] =
		g_signal_new ("image",
			      G_TYPE_FROM_INTERFACE (iface),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlayerIface, image),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      2,
			      G_TYPE_POINTER, GDK_TYPE_PIXBUF);

	/**
	 * RBPlayer::redirect:
	 * @player: the #RBPlayer
	 * @stream_data: data associated with the stream
	 * @uri: URI to redirect to
	 *
	 * The 'redirect' signal is emitted to indicate when a stream has change URI.
	 */
	signals[REDIRECT] =
		g_signal_new ("redirect",
			      G_TYPE_FROM_INTERFACE (iface),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBPlayerIface, redirect),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      2,
			      G_TYPE_POINTER, G_TYPE_STRING);
}

GType
rb_player_get_type (void)
{
	static GType our_type = 0;

	if (!our_type) {
		static const GTypeInfo our_info = {
			sizeof (RBPlayerIface),
			NULL,	/* base_init */
			NULL,	/* base_finalize */
			(GClassInitFunc)rb_player_interface_init,
			NULL,	/* class_finalize */
			NULL,	/* class_data */
			0,
			0,
			NULL
		};

		our_type = g_type_register_static (G_TYPE_INTERFACE, "RBPlayer", &our_info, 0);
	}

	return our_type;
}

/**
 * rb_player_open:
 * @player:	a #RBPlayer
 * @uri:	URI to open
 * @stream_data: arbitrary data to associate with the stream
 * @stream_data_destroy: function to call to destroy the stream data
 * @error:	returns error information
 *
 * Prepares a stream for playback.  Depending on the player
 * implementation, this may stop any existing stream being
 * played.  The stream preparation process may continue
 * asynchronously, in which case errors may be reported from
 * #rb_player_play or using the 'error' signal.
 *
 * Return value: TRUE if the stream preparation was not unsuccessful
 */
gboolean
rb_player_open (RBPlayer *player, const char *uri, gpointer stream_data, GDestroyNotify stream_data_destroy, GError **error)
{
	RBPlayerIface *iface = RB_PLAYER_GET_IFACE (player);

	return iface->open (player, uri, stream_data, stream_data_destroy, error);
}

/**
 * rb_player_opened:
 * @player: 	a #RBPlayer
 *
 * Determines whether a stream has been prepared for playback.
 *
 * Return value: TRUE if a stream is prepared for playback
 */
gboolean
rb_player_opened (RBPlayer *player)
{
	RBPlayerIface *iface = RB_PLAYER_GET_IFACE (player);

	return iface->opened (player);
}

/**
 * rb_player_close:
 * @player:	a #RBPlayer
 * @uri:	optionally, the URI of the stream to close
 * @error:	returns error information
 *
 * If a URI is specified, this will close the stream corresponding
 * to that URI and free any resources related resources.  If @uri
 * is NULL, this will close all streams.
 *
 * If no streams remain open after this call, the audio device will
 * be released.
 *
 * Return value: TRUE if a stream was found and closed
 */
gboolean
rb_player_close (RBPlayer *player, const char *uri, GError **error)
{
	RBPlayerIface *iface = RB_PLAYER_GET_IFACE (player);

	return iface->close (player, uri, error);
}

/**
 * rb_player_play:
 * @player:	a #RBPlayer
 * @play_type:  requested playback start type
 * @crossfade:	requested crossfade duration (nanoseconds)
 * @error:	returns error information
 *
 * Starts playback of the most recently opened stream.
 * if @play_type is #RB_PLAYER_PLAY_CROSSFADE, the player
 * may attempt to crossfade the new stream with any existing
 * streams.  If it does this, the it will use @crossfade as the
 * duration of the fade.
 *
 * If @play_type is #RB_PLAYER_PLAY_AFTER_EOS, the player may
 * attempt to start the stream immediately after the current
 * playing stream reaches EOS.  This may or may not result in
 * the phenomemon known as 'gapless playback'.
 *
 * If @play_type is #RB_PLAYER_PLAY_REPLACE, the player will stop any
 * existing stream before starting the new stream. It may do
 * this anyway, regardless of the value of @play_type.
 *
 * The 'playing-stream' signal will be emitted when the new stream
 * is actually playing. This may be before or after control returns
 * to the caller.
 *
 * Return value: %TRUE if playback started successfully
 */
gboolean
rb_player_play (RBPlayer *player, RBPlayerPlayType play_type, gint64 crossfade, GError **error)
{
	RBPlayerIface *iface = RB_PLAYER_GET_IFACE (player);

	return iface->play (player, play_type, crossfade, error);
}

/**
 * rb_player_pause:
 * @player:	a #RBPlayer
 *
 * Pauses playback of the most recently started stream.  Any
 * streams being faded out may continue until the fade is
 * complete.
 */
void
rb_player_pause (RBPlayer *player)
{
	RBPlayerIface *iface = RB_PLAYER_GET_IFACE (player);

	iface->pause (player);
}

/**
 * rb_player_playing:
 * @player:	a #RBPlayer.
 *
 * Determines whether the player is currently playing a stream.
 * A stream is playing if it's not paused or being faded out.
 *
 * Return value: TRUE if playing
 */
gboolean
rb_player_playing (RBPlayer *player)
{
	RBPlayerIface *iface = RB_PLAYER_GET_IFACE (player);

	return iface->playing (player);
}

/**
 * rb_player_set_volume:
 * @player:	a #RBPlayer
 * @volume:	new output volume level
 *
 * Adjusts the output volume level.  This affects all streams.
 * The player may use a hardware volume control to implement
 * this volume adjustment.
 */
void
rb_player_set_volume (RBPlayer *player, float volume)
{
	RBPlayerIface *iface = RB_PLAYER_GET_IFACE (player);

	iface->set_volume (player, volume);
}

/**
 * rb_player_get_volume:
 * @player:	a #RBPlayer
 *
 * Returns the current volume level, between 0.0 and 1.0.
 *
 * Return value: current output volume level
 */
float
rb_player_get_volume (RBPlayer *player)
{
	RBPlayerIface *iface = RB_PLAYER_GET_IFACE (player);

	return iface->get_volume (player);
}

/**
 * rb_player_seekable:
 * @player:	a #RBPlayer
 *
 * Determines whether seeking is supported for the current stream.
 *
 * Return value: TRUE if the current stream is seekable
 */
gboolean
rb_player_seekable (RBPlayer *player)
{
	RBPlayerIface *iface = RB_PLAYER_GET_IFACE (player);

	return iface->seekable (player);
}

/**
 * rb_player_set_time:
 * @player:	a #RBPlayer
 * @newtime:	seek target position in seconds
 *
 * Attempts to seek in the current stream.  The player
 * may ignore this if the stream is not seekable.
 * The seek may take place asynchronously.
 */
void
rb_player_set_time (RBPlayer *player, gint64 newtime)
{
	RBPlayerIface *iface = RB_PLAYER_GET_IFACE (player);

	iface->set_time (player, newtime);
}

/**
 * rb_player_get_time:
 * @player:	a #RBPlayer
 *
 * Returns the current playback for the current stream in nanoseconds.
 *
 * Return value: playback position
 */
gint64
rb_player_get_time (RBPlayer *player)
{
	RBPlayerIface *iface = RB_PLAYER_GET_IFACE (player);

	return iface->get_time (player);
}

/**
 * rb_player_multiple_open:
 * @player:	a #RBPlayer
 *
 * Determines whether the player supports multiple open streams.
 *
 * Return value: TRUE if multiple open is supported
 */
gboolean
rb_player_multiple_open (RBPlayer *player)
{
	RBPlayerIface *iface = RB_PLAYER_GET_IFACE (player);

	if (iface->multiple_open)
		return iface->multiple_open (player);
	else
		return FALSE;
}

/**
 * rb_player_new:
 * @want_crossfade: if TRUE, try to use a backend that supports
 * 		    crossfading and other track transitions.
 * @error:	returns error information
 *
 * Creates a new player object.
 *
 * Return value: (transfer full): new player object.
 */
RBPlayer*
rb_player_new (gboolean want_crossfade, GError **error)
{
	if (want_crossfade)
		return rb_player_gst_multi_new (error);
	else
		return rb_player_gst_new (error);
}

/**
 * _rb_player_emit_eos:
 * @player: a #RBPlayer implementation
 * @stream_data: data associated with the stream
 * @early: whether this is an early EOS notification
 *
 * Emits the 'eos' signal for a stream.  To be used by
 * implementations only.
 */
void
_rb_player_emit_eos (RBPlayer *player, gpointer stream_data, gboolean early)
{
	g_assert (rb_is_main_thread ());
	g_signal_emit (player, signals[EOS], 0, stream_data, early);
}

/**
 * _rb_player_emit_info:
 * @player: a #RBPlayer implementation
 * @stream_data: data associated with the stream
 * @field: updated metadata field
 * @value: metadata field value
 *
 * Emits the 'info' signal for a stream.  To be used by
 * implementations only.
 */
void
_rb_player_emit_info (RBPlayer *player,
		      gpointer stream_data,
		      RBMetaDataField field,
		      GValue *value)
{
	g_assert (rb_is_main_thread ());
	g_signal_emit (player, signals[INFO], 0, stream_data, field, value);
}

/**
 * _rb_player_emit_buffering:
 * @player: a #RBPlayer implementation
 * @stream_data: data associated with the stream
 * @progress: current buffering progress.
 *
 * Emits the 'buffering' signal for a stream.
 * To be used by implementations only.
 */
void
_rb_player_emit_buffering (RBPlayer *player, gpointer stream_data, guint progress)
{
	g_assert (rb_is_main_thread ());
	g_signal_emit (player, signals[BUFFERING], 0, stream_data, progress);
}

/**
 * _rb_player_emit_error:
 * @player: a #RBPlayer implementation
 * @stream_data: data associated with the stream
 * @error: playback error
 *
 * Emits the 'error' signal for a stream.
 * To be used by implementations only.
 */
void
_rb_player_emit_error (RBPlayer *player, gpointer stream_data, GError *error)
{
	g_assert (rb_is_main_thread ());
	g_signal_emit (player, signals[ERROR], 0, stream_data, error);
}

/**
 * _rb_player_emit_tick:
 * @player: a #RBPlayer implementation
 * @stream_data: data associated with the stream
 * @elapsed: current playback position
 * @duration: current perception of the duration of the stream (-1 if not applicable)
 *
 * Emits the 'tick' signal for a stream.
 * To be used by implementations only.
 */
void
_rb_player_emit_tick (RBPlayer *player, gpointer stream_data, gint64 elapsed, gint64 duration)
{
	g_assert (rb_is_main_thread ());
	g_signal_emit (player, signals[TICK], 0, stream_data, elapsed, duration);
}

/**
 * _rb_player_emit_event:
 * @player: a #RBPlayer implementation
 * @stream_data: data associated with the stream
 * @name: event name
 * @data: event data
 *
 * Emits the 'event' signal for a stream.
 * To be used by implementations only.
 */
void
_rb_player_emit_event (RBPlayer *player, gpointer stream_data, const char *name, gpointer data)
{
	g_assert (rb_is_main_thread ());
	g_signal_emit (player, signals[EVENT], g_quark_from_string (name), stream_data, data);
}

/**
 * _rb_player_emit_playing_stream:
 * @player: a #RBPlayer implementation
 * @stream_data: data associated with the new playing stream
 *
 * Emits the 'playing-stream' signal to indicate the current
 * playing stream has changed.  To be used by implementations only.
 */
void
_rb_player_emit_playing_stream (RBPlayer *player, gpointer stream_data)
{
	g_assert (rb_is_main_thread ());
	g_signal_emit (player, signals[PLAYING_STREAM], 0, stream_data);
}

/**
 * _rb_player_emit_volume_changed:
 * @player: a #RBPlayer implementation
 * @volume: the new volume level
 *
 * Emits the 'volume-changed' signal to indicate the output stream
 * volume has been changed.  To be used by implementations only.
 */
void
_rb_player_emit_volume_changed (RBPlayer *player, float volume)
{
	g_assert (rb_is_main_thread ());
	g_signal_emit (player, signals[VOLUME_CHANGED], 0, volume);
}

/**
 * _rb_player_emit_image:
 * @player: a #RBPlayer implementation
 * @stream_data: data associated with the stream
 * @image: an image extracted from the stream
 *
 * Emits the 'image' signal to notify listeners of an image that
 * has been extracted from the stream.  To be used by implementations only.
 */
void
_rb_player_emit_image (RBPlayer *player, gpointer stream_data, GdkPixbuf *image)
{
	g_assert (rb_is_main_thread ());
	g_signal_emit (player, signals[IMAGE], 0, stream_data, image);
}

/**
 * _rb_player_emit_redirect:
 * @player: a #RBPlayer implementation
 * @stream_data: data associated with the stream
 * @uri: URI to redirect to
 *
 * Emits the 'redirect' signal to notify listeners that the stream has been
 * redirected. To be used by implementations only.
 */
void
_rb_player_emit_redirect (RBPlayer *player, gpointer stream_data, const char *uri)
{
	g_assert (rb_is_main_thread ());
	g_signal_emit (player, signals[REDIRECT], 0, stream_data, uri);
}

GQuark
rb_player_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("rb_player_error");

	return quark;
}

/* This should really be standard. */
#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
rb_player_error_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)	{
		static const GEnumValue values[] = {
			ENUM_ENTRY (RB_PLAYER_ERROR_NO_AUDIO, "no-audio"),
			ENUM_ENTRY (RB_PLAYER_ERROR_GENERAL, "general-error"),
			ENUM_ENTRY (RB_PLAYER_ERROR_INTERNAL, "internal-error"),
			ENUM_ENTRY (RB_PLAYER_ERROR_NOT_FOUND, "not-found"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RBPlayerError", values);
	}

	return etype;
}

GType
rb_player_play_type_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)	{
		static const GEnumValue values[] = {
			ENUM_ENTRY (RB_PLAYER_PLAY_REPLACE, "replace"),
			ENUM_ENTRY (RB_PLAYER_PLAY_AFTER_EOS, "start-after-eos"),
			ENUM_ENTRY (RB_PLAYER_PLAY_CROSSFADE, "crossfade"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RBPlayerPlayType", values);
	}

	return etype;
}

