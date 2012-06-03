/*
 * e-source-enums.h
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

#endif /* E_SOURCE_ENUMS_H */
