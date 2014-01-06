/*
 * e-source-enums.h
 *
 * This library is free software you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#if !defined (__LIBEDATASERVER_H_INSIDE__) && !defined (LIBEDATASERVER_COMPILATION)
#error "Only <libedataserver/libedataserver.h> should be included directly."
#endif

#ifndef E_SOURCE_ENUMS_H
#define E_SOURCE_ENUMS_H

/**
 * EMdnResponsePolicy:
 * @E_MDN_RESPONSE_POLICY_NEVER:
 *   Never respond to an MDN request.
 * @E_MDN_RESPONSE_POLICY_ALWAYS:
 *   Always respond to an MDN request.
 * @E_MDN_RESPONSE_POLICY_ASK:
 *   Ask the user before responding to an MDN request.
 *
 * Policy for responding to Message Disposition Notification requests
 * (i.e. a Disposition-Notification-To header) when receiving messages.
 * See RFC 2298 for more information about MDN requests.
 *
 * Since: 3.6
 **/
typedef enum {
	E_MDN_RESPONSE_POLICY_NEVER,
	E_MDN_RESPONSE_POLICY_ALWAYS,
	E_MDN_RESPONSE_POLICY_ASK
} EMdnResponsePolicy;

/**
 * EProxyMethod:
 * @E_PROXY_METHOD_DEFAULT:
 *   Use the default #GProxyResolver (see g_proxy_resolver_get_default()).
 * @E_PROXY_METHOD_MANUAL:
 *   Use the FTP/HTTP/HTTPS/SOCKS settings defined in #ESourceProxy.
 * @E_PROXY_METHOD_AUTO:
 *   Use the autoconfiguration URL defined in #ESourceProxy.
 * @E_PROXY_METHOD_NONE:
 *   Direct connection; do not use a network proxy.
 *
 * Network proxy configuration methods.
 *
 * Since: 3.12
 **/
typedef enum {
	E_PROXY_METHOD_DEFAULT,
	E_PROXY_METHOD_MANUAL,
	E_PROXY_METHOD_AUTO,
	E_PROXY_METHOD_NONE
} EProxyMethod;

/**
 * ESourceAuthenticationResult:
 * @E_SOURCE_AUTHENTICATION_ERROR:
 *   An error occurred while authenticating.
 * @E_SOURCE_AUTHENTICATION_ACCEPTED:
 *   Server requesting authentication accepted password.
 * @E_SOURCE_AUTHENTICATION_REJECTED:
 *   Server requesting authentication rejected password.
 *
 * Status codes used by the #ESourceAuthenticator interface.
 *
 * Since: 3.6
 **/
typedef enum {
	E_SOURCE_AUTHENTICATION_ERROR,
	E_SOURCE_AUTHENTICATION_ACCEPTED,
	E_SOURCE_AUTHENTICATION_REJECTED
} ESourceAuthenticationResult;

/**
 * ETrustPromptResponse:
 *
 * XXX Document me!
 *
 * Since: 3.8
 **/
typedef enum {
	E_TRUST_PROMPT_RESPONSE_UNKNOWN = -1,
	E_TRUST_PROMPT_RESPONSE_REJECT = 0,
	E_TRUST_PROMPT_RESPONSE_ACCEPT = 1,
	E_TRUST_PROMPT_RESPONSE_ACCEPT_TEMPORARILY = 2,
	E_TRUST_PROMPT_RESPONSE_REJECT_TEMPORARILY = 3
} ETrustPromptResponse;

#endif /* E_SOURCE_ENUMS_H */
