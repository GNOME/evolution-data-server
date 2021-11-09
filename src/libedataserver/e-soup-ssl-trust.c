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
 * @include: libedataserver/libedataserver.h
 * @short_description: SSL certificate trust handling for WebDAV sources
 *
 * 
 **/

#include "evolution-data-server-config.h"

#include "e-source-authentication.h"
#include "e-source-webdav.h"

#include "e-soup-ssl-trust.h"

static gboolean
e_soup_ssl_trust_accept_certificate_cb (SoupMessage *message,
					GTlsCertificate *peer_cert,
					GTlsCertificateFlags errors,
					gpointer user_data)
{
	ESource *source = user_data;
	ETrustPromptResponse response;
	GUri *g_uri;
	const gchar *host;
	gchar *auth_host = NULL;

	g_uri = soup_message_get_uri (message);
	if (!g_uri || !g_uri_get_host (g_uri))
		return FALSE;

	host = g_uri_get_host (g_uri);

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION)) {
		ESourceAuthentication *extension_authentication;

		extension_authentication = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);
		auth_host = e_source_authentication_dup_host (extension_authentication);

		if (auth_host && *auth_host) {
			/* Use the 'host' from the Authentication extension, because
			   it's the one used when storing the trust prompt result.
			   The SoupMessage can be redirected, thus it would not ever match. */
			host = auth_host;
		} else {
			g_free (auth_host);
			auth_host = NULL;
		}
	}

	response = e_source_webdav_verify_ssl_trust (
		e_source_get_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND),
		host, peer_cert, errors);

	g_free (auth_host);

	return (response == E_TRUST_PROMPT_RESPONSE_ACCEPT ||
	        response == E_TRUST_PROMPT_RESPONSE_ACCEPT_TEMPORARILY);
}

/**
 * e_soup_ssl_trust_connect:
 * @soup_message: a #SoupMessage about to be sent to the source
 * @source: an #ESource that uses WebDAV
 *
 * Sets up automatic SSL certificate trust handling for @soup_message using the trust
 * data stored in @source's WebDAV extension. If @soup_message is about to be sent on
 * an SSL connection with an invalid certificate, the code checks if the WebDAV
 * extension already has a trust response for that certificate and verifies it
 * with e_source_webdav_verify_ssl_trust(). If the verification fails, then
 * the @soup_message send also fails.
 *
 * This works by connecting to the "network-event" signal on @soup_message and
 * connecting to the "accept-certificate" signal on each #GTlsConnection for
 * which @soup_message reports a #G_SOCKET_CLIENT_TLS_HANDSHAKING event. These
 * handlers are torn down automatically when @soup_message is disposed. This process
 * is not thread-safe; it is sufficient for safety if all use of @soup_message's
 * session and the disposal of @soup_message occur in the same thread.
 *
 * Since: 3.16
 **/
void
e_soup_ssl_trust_connect (SoupMessage *soup_message,
                          ESource *source)
{
	g_return_if_fail (SOUP_IS_MESSAGE (soup_message));
	g_return_if_fail (E_IS_SOURCE (source));

	g_signal_connect_data (
		soup_message, "accept-certificate",
		G_CALLBACK (e_soup_ssl_trust_accept_certificate_cb), g_object_ref (source),
		(GClosureNotify) g_object_unref, 0);
}
