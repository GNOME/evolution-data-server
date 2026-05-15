/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Dan Winship <danw@ximian.com>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_TRANSPORT_H
#define CAMEL_TRANSPORT_H

#include <camel/camel-address.h>
#include <camel/camel-mime-message.h>
#include <camel/camel-service.h>

/* Standard GObject macros */
#define CAMEL_TYPE_TRANSPORT \
	(camel_transport_get_type ())
#define CAMEL_TRANSPORT(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_TRANSPORT, CamelTransport))
#define CAMEL_TRANSPORT_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_TRANSPORT, CamelTransportClass))
#define CAMEL_IS_TRANSPORT(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_TRANSPORT))
#define CAMEL_IS_TRANSPORT_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_TRANSPORT))
#define CAMEL_TRANSPORT_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_TRANSPORT, CamelTransportClass))

G_BEGIN_DECLS

typedef struct _CamelTransport CamelTransport;
typedef struct _CamelTransportClass CamelTransportClass;
typedef struct _CamelTransportPrivate CamelTransportPrivate;

struct _CamelTransport {
	CamelService parent;
	CamelTransportPrivate *priv;
};

struct _CamelTransportClass {
	CamelServiceClass parent_class;

	/* Synchronous I/O Methods */
	gboolean	(*send_to_sync)		(CamelTransport *transport,
						 CamelMimeMessage *message,
						 CamelAddress *from,
						 CamelAddress *recipients,
						 gboolean *out_sent_message_saved,
						 GCancellable *cancellable,
						 GError **error);

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_transport_get_type	(void);
gboolean	camel_transport_get_request_dsn	(CamelTransport *transport);
void		camel_transport_set_request_dsn	(CamelTransport *transport,
						 gboolean request_dsn);
gboolean	camel_transport_send_to_sync	(CamelTransport *transport,
						 CamelMimeMessage *message,
						 CamelAddress *from,
						 CamelAddress *recipients,
						 gboolean *out_sent_message_saved,
						 GCancellable *cancellable,
						 GError **error);
void		camel_transport_send_to		(CamelTransport *transport,
						 CamelMimeMessage *message,
						 CamelAddress *from,
						 CamelAddress *recipients,
						 gint io_priority,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	camel_transport_send_to_finish	(CamelTransport *transport,
						 GAsyncResult *result,
						 gboolean *out_sent_message_saved,
						 GError **error);

G_END_DECLS

#endif /* CAMEL_TRANSPORT_H */
