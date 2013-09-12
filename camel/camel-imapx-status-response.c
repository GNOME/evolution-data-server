/*
 * camel-imapx-status-response.c
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

/**
 * SECTION: camel-imapx-status-response
 * @include: camel/camel.h
 * @short_description: Stores an IMAP STATUS respose
 *
 * #CamelIMAPXStatusResponse encapsulates an IMAP STATUS response, which
 * describes the current status of a mailbox in terms of various message
 * counts and change tracking indicators.
 **/

#include "camel-imapx-status-response.h"

#include <camel/camel-imapx-utils.h>

#define CAMEL_IMAPX_STATUS_RESPONSE_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_IMAPX_STATUS_RESPONSE, CamelIMAPXStatusResponsePrivate))

struct _CamelIMAPXStatusResponsePrivate {
	gchar *mailbox_name;
	guint32 messages;
	guint32 recent;
	guint32 unseen;
	guint32 uidnext;
	guint32 uidvalidity;
	guint64 highestmodseq;
};

G_DEFINE_TYPE (
	CamelIMAPXStatusResponse,
	camel_imapx_status_response,
	G_TYPE_OBJECT)

static void
imapx_status_response_finalize (GObject *object)
{
	CamelIMAPXStatusResponsePrivate *priv;

	priv = CAMEL_IMAPX_STATUS_RESPONSE_GET_PRIVATE (object);

	g_free (priv->mailbox_name);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_imapx_status_response_parent_class)->
		finalize (object);
}

static void
camel_imapx_status_response_class_init (CamelIMAPXStatusResponseClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (
		class, sizeof (CamelIMAPXStatusResponsePrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = imapx_status_response_finalize;
}

static void
camel_imapx_status_response_init (CamelIMAPXStatusResponse *response)
{
	response->priv = CAMEL_IMAPX_STATUS_RESPONSE_GET_PRIVATE (response);
}

/**
 * camel_imapx_status_response_new:
 * @stream: a #CamelIMAPXStream
 * @inbox_separator: the separator character for INBOX
 * @cancellable: a #GCancellable
 * @error: return location for a #GError, or %NULL
 *
 * Attempts to parse an IMAP STATUS response from @stream and, if successful,
 * stores the response data in a new #CamelIMAPXStatusResponse.  If an error
 * occurs, the function sets @error and returns %NULL.
 *
 * Returns: a #CamelIMAPXStatusResponse, or %NULL
 *
 * Since: 3.10
 **/
CamelIMAPXStatusResponse *
camel_imapx_status_response_new (CamelIMAPXStream *stream,
                                 gchar inbox_separator,
                                 GCancellable *cancellable,
                                 GError **error)
{
	CamelIMAPXStatusResponse *response;
	camel_imapx_token_t tok;
	guchar *token;
	guint len;

	g_return_val_if_fail (CAMEL_IS_IMAPX_STREAM (stream), NULL);

	response = g_object_new (CAMEL_TYPE_IMAPX_STATUS_RESPONSE, NULL);

	/* Parse mailbox name. */

	response->priv->mailbox_name = camel_imapx_parse_mailbox (
		stream, inbox_separator, cancellable, error);
	if (response->priv->mailbox_name == NULL)
		goto fail;

	/* Parse status attributes. */

	tok = camel_imapx_stream_token (
		stream, &token, &len, cancellable, error);
	if (tok == IMAPX_TOK_ERROR)
		goto fail;
	if (tok != '(') {
		g_set_error (
			error, CAMEL_IMAPX_ERROR, 1,
			"status: expecting '('");
		goto fail;
	}

	tok = camel_imapx_stream_token (
		stream, &token, &len, cancellable, error);

	while (tok == IMAPX_TOK_TOKEN) {
		guint64 number;
		gboolean success;

		switch (imapx_tokenise ((gchar *) token, len)) {
			case IMAPX_MESSAGES:
				success = camel_imapx_stream_number (
					stream, &number, cancellable, error);
				response->priv->messages = (guint32) number;
				break;

			case IMAPX_RECENT:
				success = camel_imapx_stream_number (
					stream, &number, cancellable, error);
				response->priv->recent = (guint32) number;
				break;

			case IMAPX_UNSEEN:
				success = camel_imapx_stream_number (
					stream, &number, cancellable, error);
				response->priv->unseen = (guint32) number;
				break;

			case IMAPX_UIDNEXT:
				success = camel_imapx_stream_number (
					stream, &number, cancellable, error);
				response->priv->uidnext = (guint32) number;
				break;

			case IMAPX_UIDVALIDITY:
				success = camel_imapx_stream_number (
					stream, &number, cancellable, error);
				response->priv->uidvalidity = (guint32) number;
				break;

			/* See RFC 4551 section 3.6 */
			case IMAPX_HIGHESTMODSEQ:
				success = camel_imapx_stream_number (
					stream, &number, cancellable, error);
				response->priv->highestmodseq = number;
				break;

			default:
				g_set_error (
					error, CAMEL_IMAPX_ERROR, 1,
					"unknown status attribute");
				success = FALSE;
				break;
		}

		if (!success)
			goto fail;

		tok = camel_imapx_stream_token (
			stream, &token, &len, cancellable, error);
	}

	if (tok == IMAPX_TOK_ERROR)
		goto fail;

	if (tok != ')') {
		g_set_error (
			error, CAMEL_IMAPX_ERROR, 1,
			"status: expecting ')' or attribute");
		goto fail;
	}

	return response;

fail:
	g_clear_object (&response);

	return NULL;
}

/**
 * camel_imapx_status_response_get_mailbox_name:
 * @response: a #CamelIMAPXStatusResponse
 *
 * Returns the mailbox name for @response.
 *
 * Returns: the mailbox name
 *
 * Since: 3.10
 **/
const gchar *
camel_imapx_status_response_get_mailbox_name (CamelIMAPXStatusResponse *response)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_STATUS_RESPONSE (response), NULL);

	return response->priv->mailbox_name;
}

/**
 * camel_imapx_status_response_get_messages:
 * @response: a #CamelIMAPXStatusResponse
 *
 * Returns the number of messages in the mailbox.
 *
 * Returns: the "MESSAGES" status value
 *
 * Since: 3.10
 **/
guint32
camel_imapx_status_response_get_messages (CamelIMAPXStatusResponse *response)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_STATUS_RESPONSE (response), 0);

	return response->priv->messages;
}

/**
 * camel_imapx_status_response_get_recent:
 * @response: a #CamelIMAPXStatusResponse
 *
 * Returns the number of messages with the \Recent flag set.
 *
 * Returns: the "RECENT" status valud
 *
 * Since: 3.10
 **/
guint32
camel_imapx_status_response_get_recent (CamelIMAPXStatusResponse *response)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_STATUS_RESPONSE (response), 0);

	return response->priv->recent;
}

/**
 * camel_imapx_status_response_get_unseen:
 * @response: a #CamelIMAPXStatusResponse
 *
 * Returns the number of messages which do no have the \Seen flag set.
 *
 * Returns: the "UNSEEN" status value
 *
 * Since: 3.10
 **/
guint32
camel_imapx_status_response_get_unseen (CamelIMAPXStatusResponse *response)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_STATUS_RESPONSE (response), 0);

	return response->priv->unseen;
}

/**
 * camel_imapx_status_response_get_uidnext:
 * @response: a #CamelIMAPXStatusResponse
 *
 * Return the next unique identifier value of the mailbox.
 *
 * Returns: the "UIDNEXT" status value
 *
 * Since: 3.10
 **/
guint32
camel_imapx_status_response_get_uidnext (CamelIMAPXStatusResponse *response)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_STATUS_RESPONSE (response), 0);

	return response->priv->uidnext;
}

/**
 * camel_imapx_status_response_get_uidvalidity:
 * @response: a #CamelIMAPXStatusResponse
 *
 * Returns the unique identifier validity value of the mailbox.
 *
 * Returns: the "UIDVALIDITY" status value
 *
 * Since: 3.10
 **/
guint32
camel_imapx_status_response_get_uidvalidity (CamelIMAPXStatusResponse *response)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_STATUS_RESPONSE (response), 0);

	return response->priv->uidvalidity;
}

/**
 * camel_imapx_status_response_get_highestmodseq:
 * @response: a #CamelIMAPXStatusResponse
 *
 * Returns the highest mod-sequence value of all messages in the mailbox, or
 * zero if the server does not support the persistent storage of mod-sequences
 * for the mailbox.
 *
 * Returns: the "HIGHESTMODSEQ" status value
 *
 * Since: 3.10
 **/
guint64
camel_imapx_status_response_get_highestmodseq (CamelIMAPXStatusResponse *response)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_STATUS_RESPONSE (response), 0);

	return response->priv->highestmodseq;
}

