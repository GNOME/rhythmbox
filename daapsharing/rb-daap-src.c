/*
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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "config.h"

#include "rb-daap-src.h"
#include "rb-daap-source.h"

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

#include <libgnome/gnome-i18n.h>
#include <gst/gst.h>

#define RB_TYPE_DAAP_SRC (rb_daap_src_get_type())
#define RB_DAAP_SRC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),RB_TYPE_DAAP_SRC,RBDAAPSrc))
#define RB_DAAP_SRC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),RB_TYPE_DAAP_SRC,RBDAAPSrcClass))
#define RB_IS_DAAP_SRC(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),RB_TYPE_DAAP_SRC))
#define RB_IS_DAAP_SRC_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),RB_TYPE_DAAP_SRC))

typedef enum {
	RB_DAAP_SRC_OPEN = GST_ELEMENT_FLAG_LAST,

	RB_DAAP_SRC_FLAG_LAST = GST_ELEMENT_FLAG_LAST + 2
} RBDAAPSrcFlags;

typedef struct _RBDAAPSrc RBDAAPSrc;
typedef struct _RBDAAPSrcClass RBDAAPSrcClass;

struct _RBDAAPSrc
{
	GstElement element;

	/* pads */
	GstPad *srcpad;

	/* uri */
	gchar *http_uri;
	gchar *daap_uri;

	/* connection */
	int port;
	gchar *host;
	gchar *path;
	struct sockaddr_in server_sockaddr;
	int sock_fd;
	
	/* Seek stuff */
	gboolean need_flush;
	gboolean send_discont;
	gint64 size;
	gint64 curoffset;
	gint64 seek_bytes;
	guint32 bytes_per_read;
};

struct _RBDAAPSrcClass
{
	GstElementClass parent_class;
};

static GstElementClass *parent_class = NULL;

static void rb_daap_src_base_init (gpointer g_class);
static void rb_daap_src_class_init (RBDAAPSrcClass *klass);
static void rb_daap_src_instance_init (RBDAAPSrc *daap_src);
static void rb_daap_src_dispose (GObject *object);

static void rb_daap_src_uri_handler_init (gpointer g_iface,
			      gpointer iface_data);

static void rb_daap_src_set_property (GObject *object,
			  guint prop_id,
			  const GValue *value,
			  GParamSpec *pspec);
static void rb_daap_src_get_property (GObject *object,
		          guint prop_id,
			  GValue *value,
			  GParamSpec *pspec);

static GstCaps * rb_daap_src_getcaps (GstPad *pad);

static GstData * rb_daap_src_get (GstPad *pad);

static GstElementStateReturn rb_daap_src_change_state (GstElement *element);

static void rb_daap_src_close_file (RBDAAPSrc *src);
static gboolean rb_daap_src_open_file (RBDAAPSrc *src);
static gboolean rb_daap_src_srcpad_event (GstPad *pad,
			  GstEvent *event);
static gboolean rb_daap_src_srcpad_query (GstPad *pad,
			  GstQueryType type,
			  GstFormat *format,
			  gint64 *value);


static GstElementDetails rb_daap_src_details =
GST_ELEMENT_DETAILS ("RBDAAP Source",
	"Source/File",
	"Read a DAAP (music share) file",
	"Charles Schmidt <cschmidt2@emich.edu");



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

enum
{
  LAST_SIGNAL
};

enum
{
	ARG_0,
	ARG_LOCATION,
	ARG_SEEKABLE,
	ARG_BYTESPERREAD
};

static GType
rb_daap_src_get_type ()
{
	static GType daap_src_type = 0;

	if (!daap_src_type) {
		static const GTypeInfo daap_src_info = {
			sizeof (RBDAAPSrcClass),
			rb_daap_src_base_init,
			NULL,
			(GClassInitFunc) rb_daap_src_class_init,
			NULL,
			NULL,
			sizeof (RBDAAPSrc),
			0,
			(GInstanceInitFunc) rb_daap_src_instance_init,
		};

		static const GInterfaceInfo urihandler_info = {
			rb_daap_src_uri_handler_init,
			NULL,
			NULL
		};

		daap_src_type = g_type_register_static (GST_TYPE_ELEMENT,
						       "RBDAAPSrc",
						       &daap_src_info,
						       0);
		g_type_add_interface_static (daap_src_type,
					     GST_TYPE_URI_HANDLER,
					     &urihandler_info);
	}

	return daap_src_type;
}

static void 
rb_daap_src_base_init (gpointer g_class)
{
	GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

	gst_element_class_set_details (element_class, &rb_daap_src_details);

	return;
}

static void 
rb_daap_src_class_init (RBDAAPSrcClass *klass)
{
	GObjectClass *gobject_class;
	GstElementClass *gstelement_class;

	gobject_class = (GObjectClass *) klass;
	gstelement_class = (GstElementClass *) klass;

	parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

	gst_element_class_install_std_props (GST_ELEMENT_CLASS (klass),
		"bytesperread", ARG_BYTESPERREAD, G_PARAM_READWRITE,
		"location", ARG_LOCATION, G_PARAM_READWRITE, NULL);

	gobject_class->dispose = rb_daap_src_dispose;

	g_object_class_install_property (gobject_class,
					 ARG_SEEKABLE,
					 g_param_spec_boolean ("seekable",
						 	       "seekable",
							       "TRUE is stream is seekable",
							       TRUE,
							       G_PARAM_READABLE));

	gstelement_class->set_property = rb_daap_src_set_property;
	gstelement_class->get_property = rb_daap_src_get_property;

	gstelement_class->change_state = rb_daap_src_change_state;
}

static void
rb_daap_src_instance_init (RBDAAPSrc *src)
{
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

	src->http_uri = NULL;
	src->daap_uri = NULL;
	src->host = NULL;
	src->path = NULL;
	src->curoffset = 0;
	src->sock_fd = -1;
	src->bytes_per_read = 4096 * 2;
	src->seek_bytes = 0;
	src->send_discont = FALSE;
	src->need_flush = FALSE;
}

static void
rb_daap_src_dispose (GObject *object)
{
	RBDAAPSrc *src = RB_DAAP_SRC (object);

	if (GST_FLAG_IS_SET (src, RB_DAAP_SRC_OPEN)) {
		rb_daap_src_close_file (src);
	}

	if (src->daap_uri) {
		g_free (src->daap_uri);
		src->daap_uri = NULL;
	}

	if (src->http_uri) {
		g_free (src->http_uri);
		src->http_uri = NULL;
	}

	if (src->host) {
		g_free (src->host);
		src->host = NULL;
	}

	if (src->path) {
		g_free (src->path);
		src->path = NULL;
	}

	if (src->sock_fd != -1) {
		close (src->sock_fd);
		src->sock_fd = -1;
	}

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

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

static void
rb_daap_src_set_property (GObject *object,
			  guint prop_id,
			  const GValue *value,
			  GParamSpec *pspec)
{
	RBDAAPSrc *src = RB_DAAP_SRC (object);

	switch (prop_id) {
		case ARG_LOCATION:
			/* the element must be stopped or paused in order to do src */
			if (GST_STATE (src) == GST_STATE_PLAYING || GST_STATE (src) == GST_STATE_PAUSED) {
				break;
			}

			if (src->daap_uri) {
				g_free (src->daap_uri);
				src->daap_uri = NULL;
			}
			if (src->http_uri) {
				g_free (src->http_uri);
				src->http_uri = NULL;
			}
			if (src->host) {
				g_free (src->host);
				src->host = NULL;
			}
			if (src->path) {
				g_free (src->path);
				src->path = NULL;
			}

			if (g_value_get_string (value)) {
				const gchar *location = g_value_get_string (value);
				const gchar *pathstart = NULL;
				const gchar *hostport = NULL;
				const gchar *portstart = NULL;
				gint locationlen;

				src->daap_uri = g_strdup (location);
				src->http_uri = g_strconcat ("http", location + 4, NULL);

				locationlen = strlen (location);
				hostport = location + 7;
				pathstart = strchr (hostport, '/');

				if (pathstart) {
					src->path = g_strdup (pathstart);
				} else {
					src->path = g_strdup ("/");
					pathstart = location + locationlen;
				}
				
				portstart = strrchr (hostport, ':');
				if (portstart) {
					src->host = g_strndup (hostport, portstart - hostport);
					src->port = strtoul (portstart + 1, NULL, 0);
				} else {
					src->host = g_strndup (hostport, pathstart - hostport);
					src->port = 3869;
				}
			}
			break;
		case ARG_BYTESPERREAD:
			src->bytes_per_read = g_value_get_int (value);
			break;
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
		case ARG_LOCATION:
			g_value_set_string (value, src->daap_uri);
			break;
		case ARG_SEEKABLE:
			g_value_set_boolean (value, FALSE);
			break;
		case ARG_BYTESPERREAD:
			g_value_set_int (value, src->bytes_per_read);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

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

static glong seek_time = 0;
static glong seek_time_to_return = 0;

static char *
get_headers (RBDAAPSrc *src)
{
	RBDAAPSource *source = NULL;
	gchar *headers;

	source = rb_daap_source_find_for_uri (src->daap_uri);
	headers = rb_daap_source_get_headers (source, src->daap_uri, seek_time, &(src->seek_bytes));
	if (src->seek_bytes) {
		src->send_discont = TRUE;
		src->need_flush = TRUE;
	}
	seek_time_to_return = seek_time;
	seek_time = 0;

	return headers;
}

/* read number of bytes from a socket into a given buffer incrementally.
 * Returns number of bytes read with same semantics as read(2):
 * < 0: error, see errno
 * = 0: EOF
 * > 0: bytes read
 */
static gint
gst_tcp_socket_read (int socket, guchar *buf, size_t count)
{
	size_t bytes_read = 0;

	while (bytes_read < count) {
		ssize_t ret = read (socket, buf + bytes_read, count - bytes_read);

		if (ret < 0) {
			GST_WARNING ("error while reading: %s", g_strerror (errno));
			return ret;
		}
		if (ret == 0)
			break;
		bytes_read += ret;
	}

	GST_LOG ("read %d bytes succesfully", bytes_read);
	return bytes_read;
}

static gint
gst_tcp_socket_write (int socket, const guchar *buf, size_t count)
{
	size_t bytes_written = 0;

	while (bytes_written < count) {
		ssize_t wrote = send (socket, buf + bytes_written, count - bytes_written, MSG_NOSIGNAL);

		if (wrote < 0) {
			GST_WARNING ("error while writing: %s", g_strerror (errno));
			return wrote;
		}
		if (wrote == 0)
			break;

		bytes_written += wrote;
	}

	GST_LOG ("wrote %d bytes succesfully", bytes_written);
	return bytes_written;
}

static GstData *
rb_daap_src_get (GstPad *pad)
{
	RBDAAPSrc *src;
	size_t readsize;
	GstBuffer *buf = NULL;

	g_return_val_if_fail (pad != NULL, NULL);
	g_return_val_if_fail (GST_IS_PAD (pad), NULL);
	src = RB_DAAP_SRC (GST_OBJECT_PARENT (pad));
	g_return_val_if_fail (GST_FLAG_IS_SET (src, RB_DAAP_SRC_OPEN), NULL);


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

	buf = gst_buffer_new ();
	g_return_val_if_fail (buf, NULL);

	GST_BUFFER_DATA (buf) = g_malloc0 (src->bytes_per_read);
	g_return_val_if_fail (GST_BUFFER_DATA (buf) != NULL, NULL);

	GST_LOG_OBJECT (src, "Reading %d bytes", src->bytes_per_read);
	readsize = gst_tcp_socket_read (src->sock_fd, GST_BUFFER_DATA (buf), src->bytes_per_read);
	if (readsize < 0) {
		GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL), GST_ERROR_SYSTEM);
		gst_buffer_unref (buf);
		return GST_DATA (gst_event_new (GST_EVENT_EOS));
	}

	/* if we read 0 bytes, and we're blocking, we hit eos */
	if (readsize == 0) {
		GST_DEBUG ("blocking read returns 0, EOS");
		gst_buffer_unref (buf);
		gst_element_set_eos (GST_ELEMENT (src));
		return GST_DATA (gst_event_new (GST_EVENT_EOS));
	}
	
	GST_BUFFER_OFFSET (buf) = src->curoffset;// + src->seek_bytes;
	GST_BUFFER_SIZE (buf) = readsize;
	GST_BUFFER_TIMESTAMP (buf) = -1;

	src->curoffset += readsize;
	GST_LOG_OBJECT (src,
			"Returning buffer from _get of size %d, ts %"
			GST_TIME_FORMAT ", dur %" GST_TIME_FORMAT
			", offset %" G_GINT64_FORMAT ", offset_end %" G_GINT64_FORMAT,
			GST_BUFFER_SIZE (buf), GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
			GST_TIME_ARGS (GST_BUFFER_DURATION (buf)),
			GST_BUFFER_OFFSET (buf), GST_BUFFER_OFFSET_END (buf));
	return GST_DATA (buf);
}

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
rb_daap_src_open_file (RBDAAPSrc *src)
{
	int ret;
	gchar *request = NULL;
	gchar *headers = NULL;

	seek_time_to_return = 0;
	
	if (src->sock_fd != -1) {
		close (src->sock_fd);
	}

	if ((src->sock_fd = socket (AF_INET, SOCK_STREAM, 0)) == -1) {
		GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL), GST_ERROR_SYSTEM);
		return FALSE;
	}

	src->server_sockaddr.sin_family = AF_INET;
	src->server_sockaddr.sin_port = htons (src->port);
	src->server_sockaddr.sin_addr.s_addr = inet_addr (src->host);
	memset (&(src->server_sockaddr.sin_zero), '\0', 8);
	
	/* connect to server */
	GST_DEBUG_OBJECT (src, "connecting to server ip=%s port=%d ",
				src->host, src->port);
	ret = connect (src->sock_fd, (struct sockaddr *) &(src->server_sockaddr), sizeof (struct sockaddr));

	if (ret) {
		switch (errno) {
			case ECONNREFUSED:
				GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
						(_("Connection to %s:%d refused."), src->host, src->port),
						(NULL));
				return FALSE;
			default:
				GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
						("connect to %s:%d failed: %s", src->host, src->port,
								g_strerror (errno)));
				return FALSE;
		}
	}

	/* send request and headers */
	headers = get_headers (src);
	request = g_strdup_printf("GET %s HTTP/1.1\r\nHost: %s\r\n%s\r\n",
					src->path, src->host, headers);
	g_free (headers);
	GST_DEBUG_OBJECT(src, "sending request %s", request);
	if (gst_tcp_socket_write(src->sock_fd, (guchar *)request, strlen(request)) <= 0) {
				GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
						("sending HTTP request to %s failed: %s", src->daap_uri,
								g_strerror (errno)));
				return FALSE;
	}
	g_free(request);

	
	/* receive and discard headers */
	/* FIXME this part is slow
	 * FIXME we should, i think, find the size from these so we can use it
	 * later
	 */
	{
		guchar responseline[12];
		gint rc;
		/* receive response line (HTTP/1.x NNN) */
		if ((rc = gst_tcp_socket_read(src->sock_fd, (guchar *)responseline, sizeof(responseline))) <= 0) {
			GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
			 ("reading HTTP response from %s failed: %s", src->daap_uri,
				g_strerror (errno)));
			return FALSE;
		}
		GST_DEBUG_OBJECT(src, "got %d byte response %s", rc, responseline);

		enum response_state {
			RESPONSE_CHAR,
			RESPONSE_CR,
			RESPONSE_CRLF,
			RESPONSE_CRLFCR,
			RESPONSE_END_OF_HEADERS /* saw crlfcrlf */
		} response_state = RESPONSE_CHAR;
		while (response_state != RESPONSE_END_OF_HEADERS) {
			guchar ch;
			if (gst_tcp_socket_read(src->sock_fd, (guchar *)&ch, sizeof(ch)) <= 0) {
				GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
				 ("reading HTTP response from %s failed: %s", src->daap_uri,
					g_strerror (errno)));
				return FALSE;
			}
			switch (ch) {
				case '\n':
					switch (response_state) {
						case RESPONSE_CR: 
							response_state = RESPONSE_CRLF; 
							break;
						case RESPONSE_CRLFCR: 
							response_state = RESPONSE_END_OF_HEADERS; 
							break;
						default: 
							response_state = RESPONSE_CHAR;
							break;
					} 
					break;	
				case '\r':
					switch (response_state) {
						case RESPONSE_CRLF: 
							response_state = RESPONSE_CRLFCR; 
							break;
						default: 
							response_state = RESPONSE_CR;
							break;
					}
					break;
				default:
					response_state = RESPONSE_CHAR;
			}
		}
	}

	GST_FLAG_SET (src, RB_DAAP_SRC_OPEN);

	return TRUE;

}
	
	
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

static gboolean src_initialized = FALSE;

void
rb_daap_src_init ()
{
	if (src_initialized == FALSE) {
		seek_time = 0;
		src_initialized = TRUE;
	}
}

void
rb_daap_src_shutdown ()
{
	src_initialized = FALSE;
}

void
rb_daap_src_set_time (glong time)
{
	seek_time = time;
}

glong
rb_daap_src_get_time ()
{
	return seek_time_to_return;
}
