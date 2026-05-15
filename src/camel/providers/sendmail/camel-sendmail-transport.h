/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Dan Winship <danw@ximian.com>
 */

#ifndef CAMEL_SENDMAIL_TRANSPORT_H
#define CAMEL_SENDMAIL_TRANSPORT_H

#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_SENDMAIL_TRANSPORT \
	(camel_sendmail_transport_get_type ())
#define CAMEL_SENDMAIL_TRANSPORT(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_SENDMAIL_TRANSPORT, CamelSendmailTransport))
#define CAMEL_SENDMAIL_TRANSPORT_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_SENDMAIL_TRANSPORT, CamelSendmailTransportClass))
#define CAMEL_IS_SENDMAIL_TRANSPORT(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_SENDMAIL_TRANSPORT))
#define CAMEL_IS_SENDMAIL_TRANSPORT_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_SENDMAIL_TRANSPORT))
#define CAMEL_SENDMAIL_TRANSPORT_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_SENDMAIL_TRANSPORT, CamelSendmailTransportClass))

G_BEGIN_DECLS

typedef struct _CamelSendmailTransport CamelSendmailTransport;
typedef struct _CamelSendmailTransportClass CamelSendmailTransportClass;

struct _CamelSendmailTransport {
	CamelTransport parent;
};

struct _CamelSendmailTransportClass {
	CamelTransportClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType camel_sendmail_transport_get_type (void);

G_END_DECLS

#endif /* CAMEL_SENDMAIL_TRANSPORT_H */
