/*
 * camel-imapx-status-response.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_IMAPX_STATUS_RESPONSE_H
#define CAMEL_IMAPX_STATUS_RESPONSE_H

#include <gio/gio.h>
#include <camel/camel-imapx-stream.h>

/* Standard GObject macros */
#define CAMEL_TYPE_IMAPX_STATUS_RESPONSE \
	(camel_imapx_status_response_get_type ())
#define CAMEL_IMAPX_STATUS_RESPONSE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_IMAPX_STATUS_RESPONSE, CamelIMAPXStatusResponse))
#define CAMEL_IMAPX_STATUS_RESPONSE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_IMAPX_STATUS_RESPONSE, CamelIMAPXStatusResponseClass))
#define CAMEL_IS_IMAPX_STATUS_RESPONSE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_IMAPX_STATUS_RESPONSE))
#define CAMEL_IS_IMAPX_STATUS_RESPONSE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_IMAPX_STATUS_RESPONSE))
#define CAMEL_IMAPX_STATUS_RESPONSE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_IMAPX_STATUS_RESPONSE, CamelIMAPXStatusResponseClass))

G_BEGIN_DECLS

typedef struct _CamelIMAPXStatusResponse CamelIMAPXStatusResponse;
typedef struct _CamelIMAPXStatusResponseClass CamelIMAPXStatusResponseClass;
typedef struct _CamelIMAPXStatusResponsePrivate CamelIMAPXStatusResponsePrivate;

/**
 * CamelIMAPXStatusResponse:
 *
 * Contains only private data that should be read and manipulated using the
 * functions below.
 *
 * Since: 3.10
 **/
struct _CamelIMAPXStatusResponse {
	GObject parent;
	CamelIMAPXStatusResponsePrivate *priv;
};

struct _CamelIMAPXStatusResponseClass {
	GObjectClass parent_class;
};

GType		camel_imapx_status_response_get_type
					(void) G_GNUC_CONST;
CamelIMAPXStatusResponse *
		camel_imapx_status_response_new
					(CamelIMAPXStream *stream,
					 gchar inbox_separator,
					 GCancellable *cancellable,
					 GError **error);
const gchar *	camel_imapx_status_response_get_mailbox_name
					(CamelIMAPXStatusResponse *response);
guint32		camel_imapx_status_response_get_messages
					(CamelIMAPXStatusResponse *response);
guint32		camel_imapx_status_response_get_recent
					(CamelIMAPXStatusResponse *response);
guint32		camel_imapx_status_response_get_unseen
					(CamelIMAPXStatusResponse *response);
guint32		camel_imapx_status_response_get_uidnext
					(CamelIMAPXStatusResponse *response);
guint32		camel_imapx_status_response_get_uidvalidity
					(CamelIMAPXStatusResponse *response);
guint64		camel_imapx_status_response_get_highestmodseq
					(CamelIMAPXStatusResponse *response);

G_END_DECLS

#endif /* CAMEL_IMAPX_STATUS_RESPONSE_H */

