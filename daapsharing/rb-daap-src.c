/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Implementatin of DAAP (iTunes Music Sharing) GStreamer source
 *
 *  Copyright (C) 2005 Charles Schmidt <cschmidt2@emich.edu>
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

#include "config.h"

#include <string.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <netdb.h>
#include <unistd.h>
#include <ctype.h>

#include <libsoup/soup-headers.h>
#include <libsoup/soup-misc.h>

#include <glib/gi18n.h>
#include <gst/gst.h>
#ifdef HAVE_GSTREAMER_0_10
#include <gst/base/gstbasesrc.h>
#include <gst/base/gstpushsrc.h>
#endif

#include "rb-daap-source.h"
#include "rb-daap-src.h"
#include "rb-debug.h"

#define RB_TYPE_DAAP_SRC (rb_daap_src_get_type())
#define RB_DAAP_SRC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),RB_TYPE_DAAP_SRC,RBDAAPSrc))
#define RB_DAAP_SRC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),RB_TYPE_DAAP_SRC,RBDAAPSrcClass))
#define RB_IS_DAAP_SRC(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),RB_TYPE_DAAP_SRC))
#define RB_IS_DAAP_SRC_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),RB_TYPE_DAAP_SRC))

#define RESPONSE_BUFFER_SIZE	(4096)

#ifdef HAVE_GSTREAMER_0_8
typedef enum {
	RB_DAAP_SRC_OPEN = GST_ELEMENT_FLAG_LAST,

	RB_DAAP_SRC_FLAG_LAST = GST_ELEMENT_FLAG_LAST + 2
} RBDAAPSrcFlags;
#endif

typedef struct _RBDAAPSrc RBDAAPSrc;
typedef struct _RBDAAPSrcClass RBDAAPSrcClass;

struct _RBDAAPSrc
{
#ifdef HAVE_GSTREAMER_0_8
	GstElement element;
	GstPad *srcpad;
#else
	GstPushSrc parent;
#endif

	/* uri */
	gchar *daap_uri;

	/* connection */
	int sock_fd;
	gchar *buffer_base;
	gchar *buffer;
	guint buffer_size;
	guint32 bytes_per_read;
	gboolean chunked;
	gboolean first_chunk;

	gint64 size;

	/* Seek stuff */
	gint64 curoffset;
	glong seek_time;
	gint64 seek_bytes;
	glong seek_time_to_return;
	gboolean do_seek;
#ifdef HAVE_GSTREAMER_0_8
	gboolean need_flush;
	gboolean send_discont;
#endif
};

struct _RBDAAPSrcClass
{
#ifdef HAVE_GSTREAMER_0_8
	GstElementClass parent_class;
#else
	GstPushSrcClass parent_class;
#endif
};

#ifdef HAVE_GSTREAMER_0_10
static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS_ANY);
#endif

GST_DEBUG_CATEGORY_STATIC (rb_daap_src_debug);
#define GST_CAT_DEFAULT rb_daap_src_debug

static GstElementDetails rb_daap_src_details =
GST_ELEMENT_DETAILS ("RBDAAP Source",
	"Source/File",
	"Read a DAAP (music share) file",
	"Charles Schmidt <cschmidt2@emich.edu");

static void rb_daap_src_uri_handler_init (gpointer g_iface, gpointer iface_data);

static void
_do_init (GType daap_src_type)
{
	static const GInterfaceInfo urihandler_info = {
		rb_daap_src_uri_handler_init,
		NULL,
		NULL
	};
	GST_DEBUG_CATEGORY_INIT (rb_daap_src_debug,
				 "daapsrc", GST_DEBUG_FG_WHITE,
				 "Rhythmbox built in DAAP source element");

	g_type_add_interface_static (daap_src_type, GST_TYPE_URI_HANDLER,
			&urihandler_info);
}

#ifdef HAVE_GSTREAMER_0_8
GType rb_daap_src_get_type (void);
GST_BOILERPLATE_FULL (RBDAAPSrc, rb_daap_src, GstElement, GST_TYPE_ELEMENT, _do_init);
#else
GST_BOILERPLATE_FULL (RBDAAPSrc, rb_daap_src, GstElement, GST_TYPE_PUSH_SRC, _do_init);
#endif

static void rb_daap_src_finalize (GObject *object);
static void rb_daap_src_set_property (GObject *object,
			  guint prop_id,
			  const GValue *value,
			  GParamSpec *pspec);
static void rb_daap_src_get_property (GObject *object,
		          guint prop_id,
			  GValue *value,
			  GParamSpec *pspec);

#ifdef HAVE_GSTREAMER_0_8
static GstCaps *rb_daap_src_getcaps (GstPad *pad);

static GstData *rb_daap_src_get (GstPad *pad);

static GstElementStateReturn rb_daap_src_change_state (GstElement *element);

static void rb_daap_src_close_file (RBDAAPSrc *src);
static gboolean rb_daap_src_open_file (RBDAAPSrc *src);
static gboolean rb_daap_src_srcpad_event (GstPad *pad,
			  GstEvent *event);
static gboolean rb_daap_src_srcpad_query (GstPad *pad,
			  GstQueryType type,
			  GstFormat *format,
			  gint64 *value);
#else
static gboolean rb_daap_src_start (GstBaseSrc *bsrc);
static gboolean rb_daap_src_stop (GstBaseSrc *bsrc);
static gboolean rb_daap_src_is_seekable (GstBaseSrc *bsrc);
static gboolean rb_daap_src_get_size (GstBaseSrc *src, guint64 *size);
static GstFlowReturn rb_daap_src_create (GstPushSrc *psrc, GstBuffer **outbuf);
#endif

#ifdef HAVE_GSTREAMER_0_8

static const GstFormat *
rb_daap_src_get_formats (GstPad *pad)
{
	static const GstFormat formats[] = {
		GST_FORMAT_BYTES,
		0,
	};

	return formats;
}

static const GstQueryType *
rb_daap_src_get_query_types (GstPad *pad)
{
  	static const GstQueryType types[] = {
    		GST_QUERY_TOTAL,
    		GST_QUERY_POSITION,
    		0,
  	};

  	return types;
}

static const GstEventMask *
rb_daap_src_get_event_mask (GstPad *pad)
{
  	static const GstEventMask masks[] = {
//    		{GST_EVENT_SEEK, GST_SEEK_METHOD_CUR | GST_SEEK_METHOD_SET | GST_SEEK_METHOD_END | GST_SEEK_FLAG_FLUSH},
    		{GST_EVENT_FLUSH, 0},
    		{GST_EVENT_SIZE, 0},
    		{0, 0},
  	};

  	return masks;
}

#endif

enum
{
	PROP_0,
	PROP_LOCATION,
	PROP_SEEKABLE,
	PROP_BYTESPERREAD
};

static void
rb_daap_src_base_init (gpointer g_class)
{
	GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
#ifdef HAVE_GSTREAMER_0_10
	gst_element_class_add_pad_template (element_class,
		gst_static_pad_template_get (&srctemplate));
#endif
	gst_element_class_set_details (element_class, &rb_daap_src_details);
}

static void
rb_daap_src_class_init (RBDAAPSrcClass *klass)
{
#ifdef HAVE_GSTREAMER_0_8
	GObjectClass *gobject_class;
	GstElementClass *gstelement_class;

	gobject_class = (GObjectClass *) klass;
	gstelement_class = (GstElementClass *) klass;

	parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

	gst_element_class_install_std_props (GST_ELEMENT_CLASS (klass),
		"bytesperread", PROP_BYTESPERREAD, G_PARAM_READWRITE,
		"location", PROP_LOCATION, G_PARAM_READWRITE, NULL);

	gobject_class->finalize = rb_daap_src_finalize;

	g_object_class_install_property (gobject_class,
					 PROP_SEEKABLE,
					 g_param_spec_boolean ("seekable",
						 	       "seekable",
							       "TRUE if stream is seekable",
							       TRUE,
							       G_PARAM_READABLE));

	gstelement_class->set_property = rb_daap_src_set_property;
	gstelement_class->get_property = rb_daap_src_get_property;

	gstelement_class->change_state = rb_daap_src_change_state;
#else
	GObjectClass *gobject_class;
	GstElementClass *gstelement_class;
	GstBaseSrcClass *gstbasesrc_class;
	GstPushSrcClass *gstpushsrc_class;

	gobject_class = G_OBJECT_CLASS (klass);
	gstelement_class = GST_ELEMENT_CLASS (klass);
	gstbasesrc_class = (GstBaseSrcClass *) klass;
	gstpushsrc_class = (GstPushSrcClass *) klass;

	parent_class = g_type_class_ref (GST_TYPE_PUSH_SRC);

	gobject_class->set_property = rb_daap_src_set_property;
	gobject_class->get_property = rb_daap_src_get_property;
	gobject_class->finalize = rb_daap_src_finalize;

	g_object_class_install_property (gobject_class, PROP_LOCATION,
			g_param_spec_string ("location",
					     "file location",
					     "location of the file to read",
					     NULL,
					     G_PARAM_READWRITE));

	gstbasesrc_class->start = GST_DEBUG_FUNCPTR (rb_daap_src_start);
	gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (rb_daap_src_stop);
	gstbasesrc_class->is_seekable = GST_DEBUG_FUNCPTR (rb_daap_src_is_seekable);
	gstbasesrc_class->get_size = GST_DEBUG_FUNCPTR (rb_daap_src_get_size);

	gstpushsrc_class->create = GST_DEBUG_FUNCPTR (rb_daap_src_create);
#endif
}

#ifdef HAVE_GSTREAMER_0_8
static void
rb_daap_src_init (RBDAAPSrc *src)
#else
static void
rb_daap_src_init (RBDAAPSrc *src, RBDAAPSrcClass *klass)
#endif
{
	src->daap_uri = NULL;
	src->sock_fd = -1;
	src->curoffset = 0;
	src->bytes_per_read = 4096 * 2;

#ifdef HAVE_GSTREAMER_0_8
	src->seek_bytes = 0;

	src->send_discont = FALSE;
	src->need_flush = FALSE;

	src->srcpad = gst_pad_new ("src", GST_PAD_SRC);
	gst_pad_set_getcaps_function (src->srcpad,
				      rb_daap_src_getcaps);
	gst_pad_set_get_function (src->srcpad,
				  rb_daap_src_get);
	gst_pad_set_event_mask_function (src->srcpad,
					 rb_daap_src_get_event_mask);
	gst_pad_set_event_function (src->srcpad,
				    rb_daap_src_srcpad_event);
	gst_pad_set_query_type_function (src->srcpad,
					 rb_daap_src_get_query_types);
	gst_pad_set_query_function (src->srcpad,
				    rb_daap_src_srcpad_query);
	gst_pad_set_formats_function (src->srcpad,
				      rb_daap_src_get_formats);
	gst_element_add_pad (GST_ELEMENT (src), src->srcpad);
#endif
}

static void
rb_daap_src_finalize (GObject *object)
{
	RBDAAPSrc *src;
	src = RB_DAAP_SRC (object);

#ifdef HAVE_GSTREAMER_0_8
	if (GST_FLAG_IS_SET (src, RB_DAAP_SRC_OPEN)) {
		rb_daap_src_close_file (src);
	}
#endif

	g_free (src->daap_uri);
	src->daap_uri = NULL;

	if (src->sock_fd != -1)
		close (src->sock_fd);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_daap_src_set_property (GObject *object,
			  guint prop_id,
			  const GValue *value,
			  GParamSpec *pspec)
{
	RBDAAPSrc *src = RB_DAAP_SRC (object);

	switch (prop_id) {
		case PROP_LOCATION:
#ifdef HAVE_GSTREAMER_0_8
			/* the element must be stopped or paused in order to do src */
			if (GST_STATE (src) == GST_STATE_PLAYING || GST_STATE (src) == GST_STATE_PAUSED) {
				break;
			}
#else
			/* XXX check stuff */
#endif

			if (src->daap_uri) {
				g_free (src->daap_uri);
				src->daap_uri = NULL;
			}
			src->daap_uri = g_strdup (g_value_get_string (value));
			break;
#ifdef HAVE_GSTREAMER_0_8
		case PROP_BYTESPERREAD:
			src->bytes_per_read = g_value_get_int (value);
			break;
#endif
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
rb_daap_src_get_property (GObject *object,
		          guint prop_id,
			  GValue *value,
			  GParamSpec *pspec)
{
	RBDAAPSrc *src = RB_DAAP_SRC (object);

	switch (prop_id) {
		case PROP_LOCATION:
			g_value_set_string (value, src->daap_uri);
			break;
#ifdef HAVE_GSTREAMER_0_8
		case PROP_SEEKABLE:
			g_value_set_boolean (value, FALSE);
			break;
		case PROP_BYTESPERREAD:
			g_value_set_int (value, src->bytes_per_read);
			break;
#endif
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static gint
rb_daap_src_write (RBDAAPSrc *src, const guchar *buf, size_t count)
{
	size_t bytes_written = 0;

	while (bytes_written < count) {
		ssize_t wrote = send (src->sock_fd, buf + bytes_written, count - bytes_written, MSG_NOSIGNAL);

		if (wrote < 0) {
			GST_WARNING ("error while writing: %s", g_strerror (errno));
			return wrote;
		}
		if (wrote == 0)
			break;

		bytes_written += wrote;
	}

	GST_DEBUG_OBJECT (src, "wrote %d bytes succesfully", bytes_written);
	return bytes_written;
}

static gint
rb_daap_src_read (RBDAAPSrc *src, guchar *buf, size_t count)
{
	size_t bytes_read = 0;

	if (src->buffer_size > 0) {
		bytes_read = count;
		if (bytes_read > src->buffer_size)
			bytes_read = src->buffer_size;

		GST_DEBUG_OBJECT (src, "reading %d bytes from buffer", bytes_read);
		memcpy (buf, src->buffer, bytes_read);
		src->buffer += bytes_read;
		src->buffer_size -= bytes_read;

		if (src->buffer_size == 0) {
			g_free (src->buffer_base);
			src->buffer_base = NULL;
			src->buffer = NULL;
		}
	}

	while (bytes_read < count) {
		ssize_t ret = read (src->sock_fd, buf + bytes_read, count - bytes_read);

		if (ret < 0) {
			GST_WARNING ("error while reading: %s", g_strerror (errno));
			return ret;
		}
		if (ret == 0)
			break;
		bytes_read += ret;
	}

	GST_DEBUG_OBJECT (src, "read %d bytes succesfully", bytes_read);
	return bytes_read;
}

static gboolean
_expect_char (RBDAAPSrc *src, guchar expected)
{
	guchar ch;
	if (rb_daap_src_read (src, &ch, sizeof (ch)) <= 0)
		return FALSE;
	if (ch != expected) {
		GST_DEBUG_OBJECT (src, "Expected char %d next, but got %d", expected, ch);
		return FALSE;
	}
	return TRUE;
}

static gboolean
rb_daap_src_read_chunk_size (RBDAAPSrc *src, gboolean first_chunk, gint64 *chunk_size)
{
	gchar chunk_buf[30];
	gchar ch;
	gint i = 0;
	memset (&chunk_buf, 0, sizeof (chunk_buf));

	GST_DEBUG_OBJECT (src, "reading next chunk size; first_chunk = %d", first_chunk);
	if (!first_chunk) {
		if (!_expect_char (src, '\r') ||
		    !_expect_char (src, '\n')) {
			return FALSE;
		}
	}

	while (1) {
		if (rb_daap_src_read (src, (guchar *)&ch, sizeof(ch)) <= 0)
			return FALSE;

		if (ch == '\r') {
			if (!_expect_char (src, '\n')) {
				return FALSE;
			}
			*chunk_size = strtoul (chunk_buf, NULL, 16);
			if (*chunk_size == 0) {
				/* EOS */
				GST_DEBUG_OBJECT (src, "got EOS chunk");
				return TRUE;
			} else if (*chunk_size == ULONG_MAX) {
				/* overflow */
				GST_DEBUG_OBJECT (src, "HTTP chunk size overflowed");
				return FALSE;
			}

			GST_DEBUG_OBJECT (src, "got HTTP chunk size %lu", *chunk_size);
			return TRUE;
		} else if (isxdigit (ch)) {
			chunk_buf[i++] = ch;
		} else {
			GST_DEBUG_OBJECT (src, "HTTP chunk size included illegal character %c", ch);
			return FALSE;
		}
	}

	g_assert_not_reached ();
}

static void
_split_uri (const gchar *daap_uri, gchar **host, guint *port, gchar **path)
{
	gint locationlen;
	const gchar *pathstart = NULL;
	const gchar *hostport = NULL;
	const gchar *portstart = NULL;

	locationlen = strlen (daap_uri);
	hostport = daap_uri + 7;
	pathstart = strchr (hostport, '/');

	if (pathstart) {
		*path = g_strdup (pathstart);
	} else {
		*path = g_strdup ("/");
		pathstart = daap_uri + locationlen;
	}

	portstart = strrchr (hostport, ':');
	if (portstart) {
		*host = g_strndup (hostport, portstart - hostport);
		*port = strtoul (portstart + 1, NULL, 0);
	} else {
		*host = g_strndup (hostport, pathstart - hostport);
		*port = 3869;
	}
}

#ifdef HAVE_GSTREAMER_0_8
/* I tell you, it'd be nice if Gstreamer's typefind element actually listened
 * to this stuff, then it wouldnt have to do all that seeking and testing
 * the streams against the typefind functions.  Here I am, /telling/ it what
 * sort of stream it is, and it can't even listen.  Bah.
 */
static GstCaps *
rb_daap_src_getcaps (GstPad *pad)
{
	RBDAAPSrc *src = NULL;
	const gchar *extension = NULL;
	static GstStaticCaps mp3_caps = GST_STATIC_CAPS ("audio/mpeg, mpegversion = (int) 1, layer = (int) [ 1, 3 ]");
	static GstStaticCaps ogg_caps = GST_STATIC_CAPS ("application/ogg");
	static GstStaticCaps wav_caps = GST_STATIC_CAPS ("audio/x-wav");
	static GstStaticCaps m4a_caps = GST_STATIC_CAPS ("audio/x-m4a");
	static GstStaticCaps aac_caps = GST_STATIC_CAPS ("audio/mpeg, mpegversion = (int) { 2, 4 }, framed = (bool) false");

	src = RB_DAAP_SRC (GST_OBJECT_PARENT (pad));

	if (src->daap_uri == NULL) {
		return gst_caps_new_any ();
	}

	extension = strrchr (src->daap_uri, '.');
	extension++;

	if ((g_strncasecmp (extension, "mp3", 3) == 0) ||
	    (g_strncasecmp (extension, "mp2", 3) == 0) ||
	    (g_strncasecmp (extension, "mp1", 3) == 0) ||
	    (g_strncasecmp (extension, "mpga", 4) == 0)) {
		return gst_caps_copy (gst_static_caps_get(&mp3_caps));
	}

	if ((g_strncasecmp (extension, "ogg", 3) == 0) ||
	    (g_strncasecmp (extension, "oggm", 4) == 0)) {
		return gst_caps_copy (gst_static_caps_get(&ogg_caps));
	}

	if ((g_strncasecmp (extension, "wav", 3) == 0)) {
		return gst_caps_copy (gst_static_caps_get(&wav_caps));
	}

	if ((g_strncasecmp (extension, "m4a", 3) == 0)) {
		return gst_caps_copy (gst_static_caps_get(&m4a_caps));
	}

	if ((g_strncasecmp (extension, "aac", 3) == 0)) {
		return gst_caps_copy (gst_static_caps_get(&aac_caps));
	}

	return gst_caps_new_any ();
}
#endif

static gboolean
rb_daap_src_open (RBDAAPSrc *src)
{
	int ret;
	struct sockaddr_in server;
	RBDAAPSource *source;
	gchar *headers;
	gchar *host;
	guint port;
	gchar *path;
	GHashTable *header_table;
	gchar *request;
	gchar *response;
	gchar *end_headers;
	size_t readsize;
	gboolean ok = TRUE;
	guint http_status;
	gchar *http_status_phrase = NULL;

	if (src->buffer_base) {
		g_free (src->buffer_base);
		src->buffer_base = NULL;
		src->buffer = NULL;
		src->buffer_size = 0;
	}

	rb_debug ("Connecting to DAAP source: %s", src->daap_uri);

	/* connect */
	src->sock_fd = socket (AF_INET, SOCK_STREAM, 0);
	if (src->sock_fd == -1) {
		GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL), GST_ERROR_SYSTEM);
		return FALSE;
	}

	_split_uri (src->daap_uri, &host, &port, &path);

	server.sin_family = AF_INET;
	server.sin_port = htons (port);
	server.sin_addr.s_addr = inet_addr (host);
	memset (&server.sin_zero, 0, sizeof (server.sin_zero));

	GST_DEBUG_OBJECT (src, "connecting to server %s:%d", host, port);
	ret = connect (src->sock_fd, (struct sockaddr *) &server, sizeof (struct sockaddr));
	if (ret) {
		if (errno == ECONNREFUSED) {
			GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
					   (_("Connection to %s:%d refused."), host, port),
					   (NULL));
		} else {
			GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
					   ("Connect to %s:%d failed: %s", host, port,
					    g_strerror (errno)));
		}
		g_free (host);
		g_free (path);
		return FALSE;
	}

	/* construct request */
	source = rb_daap_source_find_for_uri (src->daap_uri);
	if (source == NULL) {
		g_warning ("Unable to lookup source for URI: %s", src->daap_uri);
		return FALSE;
	}

	/* The following can fail if the source is no longer connected */
	headers = rb_daap_source_get_headers (source, src->daap_uri, src->seek_time, &src->seek_bytes);
	if (headers == NULL) {
		g_free (host);
		g_free (path);
		return FALSE;
	}

	request = g_strdup_printf ("GET %s HTTP/1.1\r\nHost: %s\r\n%s\r\n",
				   path, host, headers);
	g_free (headers);
	g_free (host);
	g_free (path);

	/* send request */
	GST_DEBUG_OBJECT (src, "Sending HTTP request:\n%s", request);
	if (rb_daap_src_write (src, (guchar *)request, strlen (request)) <= 0) {
		GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
				   ("Sending HTTP request to %s failed: %s",
				    src->daap_uri, g_strerror (errno)));
		g_free (request);
		return FALSE;
	}
	g_free (request);

	/* read response */
	response = g_malloc0 (RESPONSE_BUFFER_SIZE + 1);
	readsize = rb_daap_src_read (src, (guchar *)response, RESPONSE_BUFFER_SIZE);
	if (readsize <= 0) {
		g_free (response);
		GST_DEBUG_OBJECT (src, "Error while reading HTTP response header");
		return FALSE;
	}
	response[readsize] = '\0';
	GST_DEBUG_OBJECT (src, "Got HTTP response:\n%s", response);

	end_headers = strstr (response, "\r\n\r\n");
	if (!end_headers) {
		/* this means the DAAP server returned more than 4k of headers.
		 * not terribly likely.
		 */
		g_free (response);
		GST_DEBUG_OBJECT (src, "HTTP response header way too long");
		return FALSE;
	}

	/* libsoup wants the headers null-terminated, despite taking a parameter
	 * specifying how long they are.
	 */
	end_headers[2] = '\0';
	end_headers += 4;

	header_table = g_hash_table_new (soup_str_case_hash, soup_str_case_equal);
	if (soup_headers_parse_response (response,
					 (end_headers - response),
					 header_table,
					 NULL,
					 &http_status,
					 &http_status_phrase)) {
		if (http_status == 200 || http_status == 206) {
			GSList *val;

			val = g_hash_table_lookup (header_table, "Transfer-Encoding");
			if (val) {
				if (g_strcasecmp ((gchar *)val->data, "chunked") == 0) {
					src->chunked = TRUE;
				} else {
					GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
							   ("Unknown HTTP transfer encoding \"%s\"", val->data));
				}
			} else {
				src->chunked = FALSE;
				val = g_hash_table_lookup (header_table, "Content-Length");
				if (val) {
					char *e;
					src->size = strtoul ((char *)val->data, &e, 10);
					if (e == val->data) {
						GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
								   ("Couldn't read HTTP content length \"%s\"", val->data));
						ok = FALSE;
					}
				}
			}

		} else {
			GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
					   ("HTTP error: %s", http_status_phrase),
					   (NULL));
			ok = FALSE;
		}
	} else {
		GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
				   ("Unable to parse HTTP response"));
		ok = FALSE;
	}
	g_free (http_status_phrase);
	g_hash_table_destroy (header_table);

	/* copy remaining data into a new buffer */
	if (ok) {
		src->buffer_size = readsize - (end_headers - response);
		src->buffer_base = g_malloc0 (src->buffer_size);
		src->buffer = src->buffer_base;
		memcpy (src->buffer_base, response + (readsize - src->buffer_size), src->buffer_size);
	}
	g_free (response);

	return ok;
}

static gboolean
#ifdef HAVE_GSTREAMER_0_8
rb_daap_src_open_file (RBDAAPSrc *src)
{
#else
rb_daap_src_start (GstBaseSrc *bsrc)
{
	RBDAAPSrc *src = RB_DAAP_SRC (bsrc);
#endif
	if (src->sock_fd != -1) {
		close (src->sock_fd);
	}

	src->curoffset = 0;

	if (rb_daap_src_open (src)) {
		src->buffer = src->buffer_base;
		src->seek_time_to_return = src->seek_time;
#ifdef HAVE_GSTREAMER_0_8
		if (src->seek_bytes != 0) {
			src->need_flush = TRUE;
			src->send_discont = TRUE;
		}
		GST_FLAG_SET (src, RB_DAAP_SRC_OPEN);
#else
		src->curoffset = src->seek_bytes;
#endif
		if (src->chunked) {
			src->first_chunk = TRUE;
			src->size = 0;
		}
		return TRUE;
	} else {
		return FALSE;
	}
}

#ifdef HAVE_GSTREAMER_0_8
static void
rb_daap_src_close_file (RBDAAPSrc *src)
{
	if (src->sock_fd != -1) {
		close (src->sock_fd);
		src->sock_fd = -1;
	}
	src->seek_bytes = 0;
	src->curoffset = 0;
	src->size = 0;
	src->send_discont = FALSE;

	GST_FLAG_UNSET (src, RB_DAAP_SRC_OPEN);
}
#else
static gboolean
rb_daap_src_stop (GstBaseSrc *bsrc)
{
	/* don't do anything - this seems to get called during setup, but
	 * we don't get started again afterwards.
	 */
	return TRUE;
}
#endif

#ifdef HAVE_GSTREAMER_0_8
static GstData *
rb_daap_src_get (GstPad *pad)
#else
static GstFlowReturn
rb_daap_src_create (GstPushSrc *psrc, GstBuffer **outbuf)
#endif
{
	RBDAAPSrc *src;
	size_t readsize;
	GstBuffer *buf = NULL;

#ifdef HAVE_GSTREAMER_0_8
	g_return_val_if_fail (pad != NULL, NULL);
	g_return_val_if_fail (GST_IS_PAD (pad), NULL);
	src = RB_DAAP_SRC (GST_OBJECT_PARENT (pad));
	g_return_val_if_fail (GST_FLAG_IS_SET (src, RB_DAAP_SRC_OPEN), NULL);
#else
	src = RB_DAAP_SRC (psrc);
#endif

	if (src->do_seek) {
		if (src->sock_fd != -1) {
			close (src->sock_fd);
			src->sock_fd = -1;
		}
#ifdef HAVE_GSTREAMER_0_8
		if (!rb_daap_src_open_file (src))
			return GST_DATA (gst_event_new (GST_EVENT_EOS));
#else
		if (!rb_daap_src_start (GST_BASE_SRC (src)))
			return GST_FLOW_ERROR;
#endif
		src->do_seek = FALSE;
	}

#ifdef HAVE_GSTREAMER_0_8
	/* try to negotiate here */
	if (!gst_pad_is_negotiated (pad)) {
		if (GST_PAD_LINK_FAILED (gst_pad_renegotiate (pad))) {
			GST_ELEMENT_ERROR (src, CORE, NEGOTIATION, (NULL), GST_ERROR_SYSTEM);
			gst_buffer_unref (buf);
			return GST_DATA (gst_event_new (GST_EVENT_EOS));
		}
	}

	if (src->need_flush) {
		GstEvent *event = gst_event_new_flush ();

		src->need_flush = FALSE;
		return GST_DATA (event);
	}

	if (src->send_discont) {
		GstEvent *event;

		src->send_discont = FALSE;
		event = gst_event_new_discontinuous (FALSE, GST_FORMAT_BYTES, src->curoffset + src->seek_bytes, NULL);
		return GST_DATA (event);
	}
#endif

	/* get a new chunk, if we need one */
	if (src->chunked && src->size == 0) {
		if (!rb_daap_src_read_chunk_size (src, src->first_chunk, &src->size)) {
#ifdef HAVE_GSTREAMER_0_8
			return GST_DATA (gst_event_new (GST_EVENT_EOS));
#else
			return GST_FLOW_ERROR;
#endif
		} else if (src->size == 0) {
			/* EOS */
#ifdef HAVE_GSTREAMER_0_8
			gst_element_set_eos (GST_ELEMENT (src));
			return GST_DATA (gst_event_new (GST_EVENT_EOS));
#else
			return GST_FLOW_UNEXPECTED;
#endif
		}
		src->first_chunk = FALSE;
	}

	readsize = src->bytes_per_read;
	if (src->chunked && readsize > src->size)
		readsize = src->size;

#ifdef HAVE_GSTREAMER_0_8
	buf = gst_buffer_new ();
	g_return_val_if_fail (buf, NULL);
	GST_BUFFER_DATA (buf) = g_malloc0 (readsize);
	g_return_val_if_fail (GST_BUFFER_DATA (buf) != NULL, NULL);
#else
	buf = gst_buffer_new_and_alloc (readsize);
#endif

	GST_LOG_OBJECT (src, "Reading %d bytes", readsize);
	readsize = rb_daap_src_read (src, GST_BUFFER_DATA (buf), readsize);
	if (readsize < 0) {
		GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL), GST_ERROR_SYSTEM);
		gst_buffer_unref (buf);
#ifdef HAVE_GSTREAMER_0_8
		return GST_DATA (gst_event_new (GST_EVENT_EOS));
#else
		return GST_FLOW_ERROR;
#endif
	}

	if (readsize == 0) {
		GST_DEBUG ("blocking read returns 0, EOS");
		gst_buffer_unref (buf);
#ifdef HAVE_GSTREAMER_0_8
		gst_element_set_eos (GST_ELEMENT (src));
		return GST_DATA (gst_event_new (GST_EVENT_EOS));
#else
		return GST_FLOW_UNEXPECTED;
#endif
	}

	if (src->chunked)
		src->size -= readsize;

	GST_BUFFER_OFFSET (buf) = src->curoffset;
	GST_BUFFER_SIZE (buf) = readsize;
	GST_BUFFER_TIMESTAMP (buf) = GST_CLOCK_TIME_NONE;
	src->curoffset += readsize;

	GST_LOG_OBJECT (src,
			"Returning buffer from _get of size %d, ts %"
			GST_TIME_FORMAT ", dur %" GST_TIME_FORMAT
			", offset %" G_GINT64_FORMAT ", offset_end %" G_GINT64_FORMAT,
			GST_BUFFER_SIZE (buf), GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
			GST_TIME_ARGS (GST_BUFFER_DURATION (buf)),
			GST_BUFFER_OFFSET (buf), GST_BUFFER_OFFSET_END (buf));
#ifdef HAVE_GSTREAMER_0_8
	return GST_DATA (buf);
#else
	*outbuf = buf;
	return GST_FLOW_OK;
#endif
}

#ifdef HAVE_GSTREAMER_0_8
static GstElementStateReturn
rb_daap_src_change_state (GstElement *element)
{
	g_return_val_if_fail (RB_IS_DAAP_SRC (element), GST_STATE_FAILURE);

	switch (GST_STATE_TRANSITION (element)) {
		case GST_STATE_READY_TO_PAUSED:
			if (!GST_FLAG_IS_SET (element, RB_DAAP_SRC_OPEN)) {
				if (!rb_daap_src_open_file (RB_DAAP_SRC (element))) {
					return GST_STATE_FAILURE;
				}
			}
			break;
		case GST_STATE_PAUSED_TO_READY:
			if (GST_FLAG_IS_SET (element, RB_DAAP_SRC_OPEN)) {
				rb_daap_src_close_file (RB_DAAP_SRC (element));
			}
			break;
		case GST_STATE_NULL_TO_READY:
		case GST_STATE_READY_TO_NULL:
		default:
			break;
	}

	if (GST_ELEMENT_CLASS (parent_class)->change_state) {
		return GST_ELEMENT_CLASS (parent_class)->change_state (element);
	}

	return GST_STATE_SUCCESS;
}

static gboolean
rb_daap_src_srcpad_event (GstPad *pad,
			  GstEvent *event)
{
	RBDAAPSrc *src = RB_DAAP_SRC (GST_PAD_PARENT (pad));

	switch (GST_EVENT_TYPE (event)) {
		case GST_EVENT_SEEK: {
			gint64 desired_offset = 0;

			if (GST_EVENT_SEEK_FORMAT (event) != GST_FORMAT_BYTES) {
				gst_event_unref (event);
				return FALSE;
			}

			switch (GST_EVENT_SEEK_METHOD (event)) {
				case GST_SEEK_METHOD_SET:
					desired_offset = (gint64) GST_EVENT_SEEK_OFFSET (event);
					break;
				case GST_SEEK_METHOD_CUR:
					desired_offset = src->curoffset + GST_EVENT_SEEK_OFFSET (event);
					break;
				case GST_SEEK_METHOD_END:
					if (src->size == 0) {
						return FALSE;
					}
					desired_offset = src->size - ABS (GST_EVENT_SEEK_OFFSET (event));
					break;
				default:
					gst_event_unref (event);
					return FALSE;
			}

			return FALSE;
			break;
		}
		case GST_EVENT_SIZE:
			if (GST_EVENT_SIZE_FORMAT (event) != GST_FORMAT_BYTES) {
				gst_event_unref (event);
				return FALSE;
			}
			src->bytes_per_read = GST_EVENT_SIZE_VALUE (event);
			g_object_notify (G_OBJECT (src), "bytesperread");
			break;
		case GST_EVENT_FLUSH:
			src->need_flush = TRUE;
			break;
		default:
			gst_event_unref (event);
			return FALSE;
			break;
	}

	gst_event_unref (event);

	return TRUE;
}

static gboolean
rb_daap_src_srcpad_query (GstPad *pad,
			  GstQueryType type,
			  GstFormat *format,
			  gint64 *value)
{
	RBDAAPSrc *src = RB_DAAP_SRC (gst_pad_get_parent (pad));

	switch (type) {
		case GST_QUERY_TOTAL:
			if (*format != GST_FORMAT_BYTES || src->size == 0) {
				return FALSE;
			}
			*value = src->size;
			break;
		case GST_QUERY_POSITION:
			switch (*format) {
				case GST_FORMAT_BYTES:
					*value = src->curoffset;
					break;
				case GST_FORMAT_PERCENT:
					return FALSE; /* FIXME */
					if (src->size == 0) {
						return FALSE;
					}
					*value = src->curoffset * GST_FORMAT_PERCENT_MAX / src->size;
					break;
				default:
					return FALSE;
			}
			break;
		default:
			return FALSE;
			break;
	}

	return TRUE;
}
#else

gboolean
rb_daap_src_is_seekable (GstBaseSrc *bsrc)
{
	return FALSE;
}

gboolean
rb_daap_src_get_size (GstBaseSrc *bsrc, guint64 *size)
{
	RBDAAPSrc *src = RB_DAAP_SRC (bsrc);
	if (!src->chunked) {
		*size = src->size;
		return TRUE;
	}
	return FALSE;
}

#endif

static gboolean
plugin_init (GstPlugin *plugin)
{
	gboolean ret = gst_element_register (plugin, "rbdaapsrc", GST_RANK_PRIMARY, RB_TYPE_DAAP_SRC);

	return ret;
}

GST_PLUGIN_DEFINE_STATIC (GST_VERSION_MAJOR,
			  GST_VERSION_MINOR,
			  "rbdaap",
			  "element to access DAAP music share files",
			  plugin_init,
			  VERSION,
			  "GPL",
			  PACKAGE,
			  "");

/*** RB DAAP SEEK INTERFACE **************************************************/

void
rb_daap_src_set_time (GstElement *element, glong time)
{
	RBDAAPSrc *src = RB_DAAP_SRC (element);
	src->seek_time = time;
	src->do_seek = TRUE;
}

glong
rb_daap_src_get_time (GstElement *element)
{
	RBDAAPSrc *src = RB_DAAP_SRC (element);
	return src->seek_time_to_return;
}

/*** GSTURIHANDLER INTERFACE *************************************************/

static guint
rb_daap_src_uri_get_type (void)
{
	return GST_URI_SRC;
}

static gchar **
rb_daap_src_uri_get_protocols (void)
{
	static gchar *protocols[] = {"daap", NULL};

	return protocols;
}

static const gchar *
rb_daap_src_uri_get_uri (GstURIHandler *handler)
{
	RBDAAPSrc *src = RB_DAAP_SRC (handler);

	return src->daap_uri;
}

static gboolean
rb_daap_src_uri_set_uri (GstURIHandler *handler,
			 const gchar *uri)
{
	RBDAAPSrc *src = RB_DAAP_SRC (handler);

	if (GST_STATE (src) == GST_STATE_PLAYING || GST_STATE (src) == GST_STATE_PAUSED) {
		return FALSE;
	}

	g_object_set (G_OBJECT (src), "location", uri, NULL);

	return TRUE;
}

static void
rb_daap_src_uri_handler_init (gpointer g_iface,
			      gpointer iface_data)
{
	GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

	iface->get_type = rb_daap_src_uri_get_type;
	iface->get_protocols = rb_daap_src_uri_get_protocols;
	iface->get_uri = rb_daap_src_uri_get_uri;
	iface->set_uri = rb_daap_src_uri_set_uri;
}
