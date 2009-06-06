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

#include <libsoup/soup.h>

#include <glib/gi18n.h>
#include <gst/gst.h>
#include <gst/base/gstbasesrc.h>
#include <gst/base/gstpushsrc.h>

#include "rb-daap-source.h"
#include "rb-daap-src.h"
#include "rb-debug.h"
#include "rb-daap-plugin.h"

/* needed for portability to some systems, e.g. Solaris */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif


#define RB_TYPE_DAAP_SRC (rb_daap_src_get_type())
#define RB_DAAP_SRC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),RB_TYPE_DAAP_SRC,RBDAAPSrc))
#define RB_DAAP_SRC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),RB_TYPE_DAAP_SRC,RBDAAPSrcClass))
#define RB_IS_DAAP_SRC(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),RB_TYPE_DAAP_SRC))
#define RB_IS_DAAP_SRC_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),RB_TYPE_DAAP_SRC))

#define RESPONSE_BUFFER_SIZE	(4096)

typedef struct _RBDAAPSrc RBDAAPSrc;
typedef struct _RBDAAPSrcClass RBDAAPSrcClass;

struct _RBDAAPSrc
{
	GstPushSrc parent;

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
	gint64 seek_bytes;
	gboolean do_seek;
};

struct _RBDAAPSrcClass
{
	GstPushSrcClass parent_class;
};

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (rb_daap_src_debug);
#define GST_CAT_DEFAULT rb_daap_src_debug

static GstElementDetails rb_daap_src_details =
GST_ELEMENT_DETAILS ("RBDAAP Source",
	"Source/File",
	"Read a DAAP (music share) file",
	"Charles Schmidt <cschmidt2@emich.edu");

static RBDaapPlugin *daap_plugin = NULL;

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

GST_BOILERPLATE_FULL (RBDAAPSrc, rb_daap_src, GstElement, GST_TYPE_PUSH_SRC, _do_init);

static void rb_daap_src_finalize (GObject *object);
static void rb_daap_src_set_property (GObject *object,
			  guint prop_id,
			  const GValue *value,
			  GParamSpec *pspec);
static void rb_daap_src_get_property (GObject *object,
		          guint prop_id,
			  GValue *value,
			  GParamSpec *pspec);

static gboolean rb_daap_src_start (GstBaseSrc *bsrc);
static gboolean rb_daap_src_stop (GstBaseSrc *bsrc);
static gboolean rb_daap_src_is_seekable (GstBaseSrc *bsrc);
static gboolean rb_daap_src_get_size (GstBaseSrc *src, guint64 *size);
static gboolean rb_daap_src_do_seek (GstBaseSrc *src, GstSegment *segment);
static GstFlowReturn rb_daap_src_create (GstPushSrc *psrc, GstBuffer **outbuf);

void
rb_daap_src_set_plugin (RBPlugin *plugin)
{
	g_assert (RB_IS_DAAP_PLUGIN (plugin));
	daap_plugin = RB_DAAP_PLUGIN (plugin);
}

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
	gst_element_class_add_pad_template (element_class,
		gst_static_pad_template_get (&srctemplate));
	gst_element_class_set_details (element_class, &rb_daap_src_details);
}

static void
rb_daap_src_class_init (RBDAAPSrcClass *klass)
{
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
	gstbasesrc_class->do_seek = GST_DEBUG_FUNCPTR (rb_daap_src_do_seek);

	gstpushsrc_class->create = GST_DEBUG_FUNCPTR (rb_daap_src_create);
}

static void
rb_daap_src_init (RBDAAPSrc *src, RBDAAPSrcClass *klass)
{
	src->daap_uri = NULL;
	src->sock_fd = -1;
	src->curoffset = 0;
	src->bytes_per_read = 4096 * 2;
}

static void
rb_daap_src_finalize (GObject *object)
{
	RBDAAPSrc *src;
	src = RB_DAAP_SRC (object);

	g_free (src->daap_uri);
	src->daap_uri = NULL;

	if (src->sock_fd != -1) {
		close (src->sock_fd);
		src->sock_fd = -1;
	}

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
			/* XXX check stuff */
			if (src->daap_uri) {
				g_free (src->daap_uri);
				src->daap_uri = NULL;
			}
			src->daap_uri = g_strdup (g_value_get_string (value));
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
		case PROP_LOCATION:
			g_value_set_string (value, src->daap_uri);
			break;
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

static gboolean
rb_daap_src_open (RBDAAPSrc *src)
{
	int ret;
	struct sockaddr *server;
	int server_len;
	RBDAAPSource *source;
	gchar *headers;
	gchar *host;
	guint port;
	gchar *path;
	SoupMessageHeaders *header_table;
	gchar *request;
	gchar *response;
	gchar *end_headers;
	size_t readsize;
	gboolean ok = TRUE;
	guint http_status;
	gchar *http_status_phrase = NULL;
	gboolean parse_result;
	SoupAddress *addr;
	SoupAddressFamily addr_family;

	if (src->buffer_base) {
		g_free (src->buffer_base);
		src->buffer_base = NULL;
		src->buffer = NULL;
		src->buffer_size = 0;
	}

	rb_debug ("Connecting to DAAP source: %s", src->daap_uri);

	_split_uri (src->daap_uri, &host, &port, &path);

	GST_DEBUG_OBJECT (src, "resolving server %s", host);
	addr = soup_address_new (host, port);
	if (soup_address_resolve_sync (addr, NULL) != SOUP_STATUS_OK) {
		GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
				("Resolving %s failed", host));
		return FALSE;
	}

	server = soup_address_get_sockaddr (addr, &server_len);
	if (server == NULL) {
		GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
				("Getting socket address from %s failed", host));
		return FALSE;
	}

	g_object_get (addr, "family", &addr_family, NULL);

	/* connect */
	src->sock_fd = socket (addr_family, SOCK_STREAM, 0);
	if (src->sock_fd == -1) {
		GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL), GST_ERROR_SYSTEM);
		return FALSE;
	}

	GST_DEBUG_OBJECT (src, "connecting to server %s:%d", host, port);
	ret = connect (src->sock_fd, server, server_len);
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

	g_object_unref (addr);

	/* construct request */
	source = rb_daap_plugin_find_source_for_uri (daap_plugin, src->daap_uri);
	if (source == NULL) {
		g_warning ("Unable to lookup source for URI: %s", src->daap_uri);
		return FALSE;
	}

	/* The following can fail if the source is no longer connected */
	headers = rb_daap_source_get_headers (source, src->daap_uri, src->seek_bytes);
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

	header_table = soup_message_headers_new (SOUP_MESSAGE_HEADERS_RESPONSE);
	parse_result = soup_headers_parse_response (response,
						    ((end_headers+2) - response),
						    header_table,
						    NULL,
						    &http_status,
						    &http_status_phrase);

	if (parse_result) {
		if (http_status == 200 || http_status == 206) {
			const char *enc_str = NULL;
			const char *len_str = NULL;
			enc_str = soup_message_headers_get (header_table, "Transfer-Encoding");
			len_str = soup_message_headers_get (header_table, "Content-Length");

			if (enc_str) {
				if (g_ascii_strcasecmp (enc_str, "chunked") == 0) {
					src->chunked = TRUE;
				} else {
					GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
							   ("Unknown HTTP transfer encoding \"%s\"", enc_str));
				}
			} else {
				src->chunked = FALSE;
				if (len_str) {
					char *e;
					src->size = strtoul (len_str, &e, 10);
					if (e == len_str) {
						GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
								   ("Couldn't read HTTP content length \"%s\"", len_str));
						ok = FALSE;
					}
				} else {
					GST_DEBUG_OBJECT (src, "Response doesn't have a content length");
					src->size = 0;
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

	soup_message_headers_free (header_table);

	end_headers += 4;

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
rb_daap_src_start (GstBaseSrc *bsrc)
{
	RBDAAPSrc *src = RB_DAAP_SRC (bsrc);
	if (src->sock_fd != -1) {
		close (src->sock_fd);
	}

	src->curoffset = 0;

	if (rb_daap_src_open (src)) {
		src->buffer = src->buffer_base;
		src->curoffset = src->seek_bytes;
		if (src->chunked) {
			src->first_chunk = TRUE;
			src->size = 0;
		}
		return TRUE;
	} else {
		return FALSE;
	}
}

static gboolean
rb_daap_src_stop (GstBaseSrc *bsrc)
{
	/* don't do anything - this seems to get called during setup, but
	 * we don't get started again afterwards.
	 */
	return TRUE;
}

static GstFlowReturn
rb_daap_src_create (GstPushSrc *psrc, GstBuffer **outbuf)
{
	RBDAAPSrc *src;
	size_t readsize;
	GstBuffer *buf = NULL;

	src = RB_DAAP_SRC (psrc);
	if (src->do_seek) {
		if (src->sock_fd != -1) {
			close (src->sock_fd);
			src->sock_fd = -1;
		}
		if (!rb_daap_src_start (GST_BASE_SRC (src)))
			return GST_FLOW_ERROR;
		src->do_seek = FALSE;
	}

	/* get a new chunk, if we need one */
	if (src->chunked && src->size == 0) {
		if (!rb_daap_src_read_chunk_size (src, src->first_chunk, &src->size)) {
			return GST_FLOW_ERROR;
		} else if (src->size == 0) {
			/* EOS */
			return GST_FLOW_UNEXPECTED;
		}
		src->first_chunk = FALSE;
	}

	readsize = src->bytes_per_read;
	if (src->chunked && readsize > src->size)
		readsize = src->size;

	buf = gst_buffer_new_and_alloc (readsize);

	GST_LOG_OBJECT (src, "Reading %d bytes", readsize);
	readsize = rb_daap_src_read (src, GST_BUFFER_DATA (buf), readsize);
	if (readsize < 0) {
		GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL), GST_ERROR_SYSTEM);
		gst_buffer_unref (buf);
		return GST_FLOW_ERROR;
	}

	if (readsize == 0) {
		GST_DEBUG ("blocking read returns 0, EOS");
		gst_buffer_unref (buf);
		return GST_FLOW_UNEXPECTED;
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
	*outbuf = buf;
	return GST_FLOW_OK;
}

gboolean
rb_daap_src_is_seekable (GstBaseSrc *bsrc)
{
	return TRUE;
}

gboolean
rb_daap_src_do_seek (GstBaseSrc *bsrc, GstSegment *segment)
{
	RBDAAPSrc *src = RB_DAAP_SRC (bsrc);
	if (segment->format == GST_FORMAT_BYTES) {
		src->do_seek = TRUE;
		src->seek_bytes = segment->start;
		return TRUE;
	} else {
		return FALSE;
	}
}

gboolean
rb_daap_src_get_size (GstBaseSrc *bsrc, guint64 *size)
{
	RBDAAPSrc *src = RB_DAAP_SRC (bsrc);
	if (src->chunked == FALSE && src->size > 0) {
		*size = src->size;
		return TRUE;
	}
	return FALSE;
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
