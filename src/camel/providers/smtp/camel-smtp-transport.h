/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Jeffrey Stedfast <fejj@stampede.org>
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

G_BEGIN_DECLS

typedef struct _CamelSmtpTransport CamelSmtpTransport;
typedef struct _CamelSmtpTransportClass CamelSmtpTransportClass;

struct _CamelSmtpTransport {
	CamelTransport parent;

	GMutex stream_lock;
	CamelStreamBuffer *istream;
	CamelStream *ostream;
	GSocketAddress *local_address;

	guint32 flags;

	gboolean need_rset;
	gboolean connected;

	GHashTable *authtypes;
};

struct _CamelSmtpTransportClass {
	CamelTransportClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType camel_smtp_transport_get_type (void);

G_END_DECLS

#endif /* CAMEL_SMTP_TRANSPORT_H */
