/*
 *  Copyright (C) 2008  Jonathan Matthew <jonathan@d14n.org>
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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  
 *  02110-1301  USA
 */

#ifndef RB_SOUP_COMPAT_H
#define RB_SOUP_COMPAT_H

#include <libsoup/soup.h>

/* compatibility junk for libsoup 2.2.
 * not intended to obviate the need for #ifdefs in code, but
 * should remove a lot of the trivial ones and make it easier
 * to drop libsoup 2.2
 */
#if defined(HAVE_LIBSOUP_2_2)

#include <libsoup/soup-uri.h>
#include <libsoup/soup-address.h>
#include <libsoup/soup-connection.h>
#include <libsoup/soup-headers.h>
#include <libsoup/soup-message.h>
#include <libsoup/soup-misc.h>
#include <libsoup/soup-session-sync.h>
#include <libsoup/soup-server.h>
#include <libsoup/soup-server-auth.h>
#include <libsoup/soup-server-message.h>


typedef SoupUri				SoupURI;
typedef SoupMessageCallbackFn		SoupSessionCallback;
typedef SoupServerContext		SoupClientContext;

#define SOUP_MEMORY_TAKE		SOUP_BUFFER_SYSTEM_OWNED
#define SOUP_MEMORY_TEMPORARY		SOUP_BUFFER_USER_OWNED

#define soup_message_headers_append	soup_message_add_header
#define soup_message_headers_get	soup_message_get_header

#define soup_client_context_get_host	soup_server_context_get_client_host

#endif	/* HAVE_LIBSOUP_2_2 */

#endif	/* RB_SOUP_COMPAT_H */

