/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
 *	    Jeffrey Stedfast <fejj@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifndef CAMEL_NET_UTILS_H
#define CAMEL_NET_UTILS_H 1

#include <sys/types.h>

#ifndef _WIN32
#include <sys/socket.h>
#include <netdb.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#undef WIN32_LEAN_AND_MEAN
#include <ws2tcpip.h>
#endif

G_BEGIN_DECLS

struct _CamelException;

#ifndef _WIN32
#ifdef NEED_ADDRINFO
/* Some of this is copied from GNU's netdb.h

  Copyright (C) 1996-2002, 2003, 2004 Free Software Foundation, Inc.
  This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.
*/
struct addrinfo {
	gint ai_flags;
	gint ai_family;
	gint ai_socktype;
	gint ai_protocol;
	gsize ai_addrlen;
	struct sockaddr *ai_addr;
	gchar *ai_canonname;
	struct addrinfo *ai_next;
};

#define AI_CANONNAME	0x0002	/* Request for canonical name.  */
#define AI_NUMERICHOST	0x0004	/* Don't use name resolution.  */

/* Error values for `getaddrinfo' function.  */
#define EAI_BADFLAGS	  -1	/* Invalid value for `ai_flags' field.  */
#define EAI_NONAME	  -2	/* NAME or SERVICE is unknown.  */
#define EAI_AGAIN	  -3	/* Temporary failure in name resolution.  */
#define EAI_FAIL	  -4	/* Non-recoverable failure in name res.  */
#define EAI_NODATA	  -5	/* No address associated with NAME.  */
#define EAI_FAMILY	  -6	/* `ai_family' not supported.  */
#define EAI_SOCKTYPE	  -7	/* `ai_socktype' not supported.  */
#define EAI_SERVICE	  -8	/* SERVICE not supported for `ai_socktype'.  */
#define EAI_ADDRFAMILY	  -9	/* Address family for NAME not supported.  */
#define EAI_MEMORY	  -10	/* Memory allocation failure.  */
#define EAI_SYSTEM	  -11	/* System error returned in `errno'.  */
#define EAI_OVERFLOW	  -12	/* Argument buffer overflow.  */

#define NI_NUMERICHOST	1	/* Don't try to look up hostname.  */
#define NI_NUMERICSERV	2	/* Don't convert port number to name.  */
#define NI_NOFQDN	4	/* Only return nodename portion.  */
#define NI_NAMEREQD	8	/* Don't return numeric addresses.  */
#define NI_DGRAM	16	/* Look up UDP service rather than TCP.  */
#endif
#endif

struct addrinfo *camel_getaddrinfo(const gchar *name, const gchar *service,
				   const struct addrinfo *hints, struct _CamelException *ex);
void camel_freeaddrinfo(struct addrinfo *host);
gint camel_getnameinfo(const struct sockaddr *sa, socklen_t salen, gchar **host, gchar **serv,
		      gint flags, struct _CamelException *ex);

G_END_DECLS

#endif /* CAMEL_NET_UTILS_H */
