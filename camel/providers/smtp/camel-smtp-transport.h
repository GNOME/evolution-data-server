/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-smtp-transport.h : class for an smtp transfer */

/*
 * Authors:
 *   Jeffrey Stedfast <fejj@stampede.org>
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

#ifndef CAMEL_SMTP_TRANSPORT_H
#define CAMEL_SMTP_TRANSPORT_H

#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_SMTP_TRANSPORT \
	(camel_smtp_transport_get_type ())
#define CAMEL_SMTP_TRANSPORT(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_SMTP_TRANSPORT, CamelSmtpTransport))
#define CAMEL_SMTP_TRANSPORT_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_SMTP_TRANSPORT, CamelSmtpTransportClass))
#define CAMEL_IS_SMTP_TRANSPORT(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_SMTP_TRANSPORT))
#define CAMEL_IS_SMTP_TRANSPORT_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_SMTP_TRANSPORT))
#define CAMEL_SMTP_TRANSPORT_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_SMTP_TRANSPORT, CamelSmtpTransportClass))

#define CAMEL_SMTP_TRANSPORT_IS_ESMTP               (1 << 0)
#define CAMEL_SMTP_TRANSPORT_8BITMIME               (1 << 1)
#define CAMEL_SMTP_TRANSPORT_ENHANCEDSTATUSCODES    (1 << 2)
#define CAMEL_SMTP_TRANSPORT_STARTTLS               (1 << 3)

#define CAMEL_SMTP_TRANSPORT_AUTH_EQUAL             (1 << 4)  /* set if we are using authtypes from a broken AUTH= */

#ifdef G_OS_WIN32
#define socklen_t int
#endif

G_BEGIN_DECLS

typedef struct _CamelSmtpTransport CamelSmtpTransport;
typedef struct _CamelSmtpTransportClass CamelSmtpTransportClass;

struct _CamelSmtpTransport {
	CamelTransport parent;

	CamelStream *istream, *ostream;

	guint32 flags;

	gboolean connected;
	struct sockaddr *localaddr;
	socklen_t localaddrlen;

	GHashTable *authtypes;
};

struct _CamelSmtpTransportClass {
	CamelTransportClass parent_class;
};

GType camel_smtp_transport_get_type (void);

G_END_DECLS

#ifdef G_OS_WIN32
#undef socklen_t
#endif

#endif /* CAMEL_SMTP_TRANSPORT_H */
