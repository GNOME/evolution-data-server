/*
 * e-soup-ssl-trust.c
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 */

/**
 * SECTION: e-soup-ssl-trust
 * @include: libebackend/libebackend.h
 * @short_description: SSL certificate trust handling for WebDAV sources
 *
 * 
 **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e-user-prompter.h"

#include "e-soup-ssl-trust.h"

typedef struct _ESoupSslTrustData {
	SoupMessage *soup_message; /* weak */
	ESource *source;
	ESourceRegistry *registry;
	GCancellable *cancellable;

	GClosure *accept_certificate_closure;
} ESoupSslTrustData;

static ETrustPromptResponse
trust_prompt_sync (const ENamedParameters *parameters,
                   GCancellable *cancellable,
                   GError **error)
{
	EUserPrompter *prompter;
	gint response;

	g_return_val_if_fail (parameters != NULL, E_TRUST_PROMPT_RESPONSE_UNKNOWN);

	prompter = e_user_prompter_new ();
	g_return_val_if_fail (prompter != NULL, E_TRUST_PROMPT_RESPONSE_UNKNOWN);

	response = e_user_prompter_extension_prompt_sync (prompter, "ETrustPrompt::trust-prompt", parameters, NULL, cancellable, error);

	g_object_unref (prompter);

	if (response == 0)
		return E_TRUST_PROMPT_RESPONSE_REJECT;
	if (response == 1)
		return E_TRUST_PROMPT_RESPONSE_ACCEPT;
	if (response == 2)
		return E_TRUST_PROMPT_RESPONSE_ACCEPT_TEMPORARILY;
	if (response == -1)
		return E_TRUST_PROMPT_RESPONSE_REJECT_TEMPORARILY;

	return E_TRUST_PROMPT_RESPONSE_UNKNOWN;
}

static gboolean
e_soup_ssl_trust_accept_certificate_cb (GTlsConnection *conn,
					GTlsCertificate *peer_cert,
					GTlsCertificateFlags errors,
					gpointer user_data)
{
	ESoupSslTrustData *handler = user_data;
	ETrustPromptResponse response;
	ENamedParameters *parameters;

	parameters = e_named_parameters_new ();

	response = e_source_webdav_prepare_ssl_trust_prompt (
		e_source_get_extension (handler->source, E_SOURCE_EXTENSION_WEBDAV_BACKEND),
		handler->soup_message, peer_cert, errors, handler->registry, parameters);
	if (response == E_TRUST_PROMPT_RESPONSE_UNKNOWN) {
		response = trust_prompt_sync (parameters, handler->cancellable, NULL);
		if (response != E_TRUST_PROMPT_RESPONSE_UNKNOWN)
			e_source_webdav_store_ssl_trust_prompt (
				e_source_get_extension (handler->source, E_SOURCE_EXTENSION_WEBDAV_BACKEND),
				handler->soup_message, peer_cert, response);
	}

	e_named_parameters_free (parameters);

	return (response == E_TRUST_PROMPT_RESPONSE_ACCEPT ||
	        response == E_TRUST_PROMPT_RESPONSE_ACCEPT_TEMPORARILY);
}

static void
e_soup_ssl_trust_network_event_cb (SoupMessage *msg,
				   GSocketClientEvent event,
				   GIOStream *connection,
				   gpointer user_data)
{
	ESoupSslTrustData *handler = user_data;

	/* It's either a GTlsConnection or a GTcpConnection */
	if (event == G_SOCKET_CLIENT_TLS_HANDSHAKING &&
	    G_IS_TLS_CONNECTION (connection)) {
		g_signal_connect_closure (
			G_TLS_CONNECTION (connection), "accept-certificate",
			handler->accept_certificate_closure, FALSE);
	}
}

static void
e_soup_ssl_trust_message_finalized_cb (gpointer data,
				       GObject *unused_message)
{
	ESoupSslTrustData *handler;

	/* The network event handler will be disconnected from the message just
	 * before this is called. */
	handler = data;

	g_clear_object (&handler->source);
	g_clear_object (&handler->registry);
	g_clear_object (&handler->cancellable);

	/* Synchronously disconnects the accept certificate handler from all
	 * GTlsConnections. */
	g_closure_invalidate (handler->accept_certificate_closure);
	g_closure_unref (handler->accept_certificate_closure);

	g_free (handler);
}

/**
 * e_soup_ssl_trust_connect:
 * @soup_message: a #SoupMessage about to be sent to the source
 * @source: an #ESource that uses WebDAV
 * @registry: (allow-none): an #ESourceRegistry, to use for parent lookups
 * @cancellable: (allow-none): #GCancellable to cancel the trust prompt
 *
 * Sets up automatic SSL certificate trust handling for @message using the trust
 * data stored in @source's WebDAV extension. If @message is about to be sent on
 * an SSL connection with an invalid certificate, the code checks if the WebDAV
 * extension already has a trust response for that certificate with
 * e_source_webdav_prepare_ssl_trust_prompt and if not, prompts the user with
 * the "ETrustPrompt::trust-prompt" extension dialog and
 * saves the result with e_source_webdav_store_ssl_trust_prompt.
 *
 * This works by connecting to the "network-event" signal on @message and
 * connecting to the "accept-certificate" signal on each #GTlsConnection for
 * which @message reports a #G_SOCKET_CLIENT_TLS_HANDSHAKING event. These
 * handlers are torn down automatically when @message is disposed. This process
 * is not thread-safe; it is sufficient for safety if all use of @message's
 * session and the disposal of @message occur in the same thread.
 *
 * Since: 3.14
 **/
void
e_soup_ssl_trust_connect (SoupMessage *soup_message,
                          ESource *source,
                          ESourceRegistry *registry,
                          GCancellable *cancellable)
{
	ESoupSslTrustData *handler;

	g_return_if_fail (SOUP_IS_MESSAGE (soup_message));
	g_return_if_fail (E_IS_SOURCE (source));

	handler = g_malloc (sizeof (ESoupSslTrustData));
	handler->soup_message = soup_message;
	g_object_weak_ref (G_OBJECT (soup_message), e_soup_ssl_trust_message_finalized_cb, handler);
	handler->source = g_object_ref (source);
	handler->registry = registry ? g_object_ref (registry) : NULL;
	handler->cancellable = cancellable ? g_object_ref (cancellable) : NULL;
	handler->accept_certificate_closure = g_cclosure_new (G_CALLBACK (e_soup_ssl_trust_accept_certificate_cb), handler, NULL);

	g_closure_ref (handler->accept_certificate_closure);
	g_closure_sink (handler->accept_certificate_closure);

	g_signal_connect (
		soup_message, "network-event",
		G_CALLBACK (e_soup_ssl_trust_network_event_cb), handler);
}
