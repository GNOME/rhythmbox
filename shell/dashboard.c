/* arch-tag: c421c4ab-1bca-452b-8fe7-24fe1b98d895 */

#ifndef __DASHBOARD_FRONTEND_H__
#define __DASHBOARD_FRONTEND_H__

#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <glib.h>
#include <strings.h>
#include <unistd.h>

#if GLIB_CHECK_VERSION (2,0,0)
#include <glib/giochannel.h>
#endif

#define DASHBOARD_PORT 5913
#define NATMIN(a,b) ((a) < (b) ? (a) : (b))

/*
 * Open a connection to the dashboard.  We never block and at
 * the first sign of a problem we bail.
 */
static int
dashboard_connect_with_timeout (int  *fd,
				long  timeout_usecs)
{
	struct sockaddr_in  sock;
	struct timeval      timeout;
	fd_set write_fds;

	*fd = socket (PF_INET, SOCK_STREAM, 0);
	if (*fd < 0) {
		perror ("Dashboard: socket");
		return 0;
	}

	/*
	 * Set the socket to be non-blocking so that connect ()
	 * doesn't block.
	 */
	if (fcntl (*fd, F_SETFL, O_NONBLOCK) < 0) {
		perror ("Dashboard: setting O_NONBLOCK");
		return 0;
	}

	bzero ((char *) &sock, sizeof (sock));
	sock.sin_family      = AF_INET;
	sock.sin_port        = htons (DASHBOARD_PORT);
	sock.sin_addr.s_addr = inet_addr ("127.0.0.1");

	timeout.tv_sec = 0;
	timeout.tv_usec = timeout_usecs;

	while (1) {

		/*
		 * Try to connect.
		 */
		if (connect (*fd, (struct sockaddr *) &sock,
			     sizeof (struct sockaddr_in)) < 0) {

			if (errno != EAGAIN &&
			    errno != EINPROGRESS) {
				return 0;
			}
				
		} else
			return 1;

		/*
		 * We couldn't connect, so we select on the fd and
		 * wait for the timer to run out, or for the fd to be
		 * ready.
		 */
		FD_ZERO (&write_fds);
		FD_SET (*fd, &write_fds);

		while (select (getdtablesize (), NULL, &write_fds, NULL, &timeout) < 0) {
			if (errno != EINTR) {
				perror ("Dashboard: select");
				return 0;
			}
		}

		if (timeout.tv_sec == 0 && timeout.tv_usec == 0) {
			fprintf (stderr, "Dashboard: Connection timed out.\n");
			return 0;
		}
		
	}

	return 1;
}

typedef struct {
	char *rawcluepacket;
	int bytes_written;
} CluepacketInfo;

#if GLIB_CHECK_VERSION(2,0,0)

static gboolean
cluepacket_write_cb (GIOChannel   *channel,
		     GIOCondition  cond,
		     gpointer      user_data)
{
	CluepacketInfo *info = user_data;
	GIOError err;
	int total_bytes;

	total_bytes = strlen (info->rawcluepacket);

	do {
		int b;

		err = g_io_channel_write (channel,
					  info->rawcluepacket + info->bytes_written,
					  total_bytes - info->bytes_written,
					  &b);
		info->bytes_written += b;
	} while (info->bytes_written < total_bytes && err == G_IO_ERROR_NONE);

	if (err == G_IO_ERROR_NONE) {
		/* We're all done sending */
		fprintf (stderr, "Dashboard: Sent.\n");
		goto cleanup;
	}

	if (err == G_IO_ERROR_AGAIN) {
		/* Hand control back to the main loop */
		return TRUE;
	}

	/* Otherwise... */
	fprintf (stderr, "Dashboard: Error trying to send cluepacket.\n");

cleanup:
	g_io_channel_close (channel);
	g_free (info->rawcluepacket);
	g_free (info);

	return FALSE;
}

static void
dashboard_send_raw_cluepacket (const char *rawcluepacket)
{
	int fd;
	GIOChannel *channel;
	CluepacketInfo *info;

	/* Connect. */
	if (! dashboard_connect_with_timeout (&fd, 200000))
		return;

	channel = g_io_channel_unix_new (fd);

	info = g_new0 (CluepacketInfo, 1);
	info->rawcluepacket = g_strdup (rawcluepacket);

	g_io_add_watch (channel,
			G_IO_OUT | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
			cluepacket_write_cb,
			info);

	g_io_channel_unref (channel);
}

#endif /* GLIB_CHECK_VERSION (2,0,0) */

#if !GLIB_CHECK_VERSION (2,0,0)
static char *
lame_xml_quote (const char *str)
{
	char       *retval;
	const char *p;
	char       *q;

	if (str == NULL || strlen (str) == 0)
		return g_strdup ("");

	retval = g_new (char, strlen (str) * 3);

	q = retval;
	for (p = str; *p != '\0'; p ++) {
		switch (*p) {

		case '<':
			*q ++ = '&';
			*q ++ = 'l';
			*q ++ = 't';
			*q ++ = ';';
			break;

		case '>':
			*q ++ = '&';
			*q ++ = 'g';
			*q ++ = 't';
			*q ++ = ';';
			break;
			
		case '&':
			*q ++ = '&';
			*q ++ = 'a';
			*q ++ = 'm';
			*q ++ = 'p';
			*q ++ = ';';
			break;
		default:
			*q ++ = *p;
			break;
		}
	}

	*q = '\0';

	return retval;
}
#endif

static char *
dashboard_xml_quote (const char *str)
{
#if GLIB_CHECK_VERSION (2,0,0)
	return g_markup_escape_text (str, strlen (str));
#else
	return lame_xml_quote (str);
#endif
}

static char *
dashboard_build_clue (const char *text,
		      const char *type,
		      int         relevance)
{
	char *text_xml;
	char *clue;

	if (text == NULL || strlen (text) == 0)
		return g_strdup ("");

	text_xml = dashboard_xml_quote (text);

	clue = g_strdup_printf ("    <Clue Type=\"%s\" Relevance=\"%d\">%s</Clue>\n",
				type, relevance, text_xml);

	g_free (text_xml);

	return clue;
}

static char *
dashboard_build_cluepacket_from_cluelist (const char *frontend,
					  gboolean    focused,
					  const char *context,
					  GList      *clues)
{
	char  *cluepacket;
	char  *new_cluepacket;
	GList *l;

	g_return_val_if_fail (frontend != NULL, NULL);
	g_return_val_if_fail (clues    != NULL, NULL);

	cluepacket = g_strdup_printf (
		"<CluePacket>\n"
		"    <Frontend>%s</Frontend>\n"
		"    <Context>%s</Context>\n"
		"    <Focused>%s</Focused>\n",
		frontend,
		context,
		focused ? "true" : "false");

	for (l = clues; l != NULL; l = l->next) {
		const char *clue = (const char *) l->data;

		new_cluepacket = g_strconcat (cluepacket, clue, NULL);
		g_free (cluepacket);

		cluepacket = new_cluepacket;
	}

	new_cluepacket = g_strconcat (cluepacket, "</CluePacket>\n", NULL);
	g_free (cluepacket);

	cluepacket = new_cluepacket;

	return cluepacket;
}

static char *
dashboard_build_cluepacket_v (const char *frontend,
			      gboolean    focused,
			      const char *context,
			      va_list     args)
{
	char    *cluep;
	GList   *clue_list;
	char    *retval;

	g_return_val_if_fail (frontend != NULL, NULL);

	cluep     = va_arg (args, char *);
	clue_list = NULL;
	while (cluep) {
		clue_list = g_list_append (clue_list, cluep);
		cluep = va_arg (args, char *);
	}

	retval = dashboard_build_cluepacket_from_cluelist (frontend, focused, context, clue_list);

	g_list_free (clue_list);

	return retval;
}

static char *
dashboard_build_cluepacket_then_free_clues (const char *frontend,
					    gboolean    focused,
					    const char *context,
					    ...)
{
	char    *retval;
	char    *cluep;
	va_list  args;

	g_return_val_if_fail (frontend != NULL, NULL);

	/* Build the cluepacket */
	va_start (args, context);
	retval = dashboard_build_cluepacket_v (frontend, focused, context, args);
	va_end (args);

	/* Free the clues */
	va_start (args, context);
	cluep = va_arg (args, char *);
	while (cluep) {
		g_free (cluep);
		cluep = va_arg (args, char *);
	}

	va_end (args);

	return retval;
}

#endif /* ! __DASHBOARD_FRONTEND_H__ */
