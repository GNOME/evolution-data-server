/*
 * SPDX-FileCopyrightText: (C) 2026 Tobias Mueller <muelli@cryptobitch.de>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#if !defined (__LIBEDATASERVER_H_INSIDE__) && !defined (LIBEDATASERVER_COMPILATION)
#error "Only <libedataserver/libedataserver.h> should be included directly."
#endif

#ifndef E_SOURCE_OAUTH2_CLIENT_H
#define E_SOURCE_OAUTH2_CLIENT_H

#include <libedataserver/e-source-extension.h>

/**
 * E_SOURCE_EXTENSION_OAUTH2_CLIENT:
 *
 * Pass this extension name to e_source_get_extension() to access
 * #ESourceOAuth2Client.  This is also used as a group name in key files.
 *
 * Since: 3.64
 **/
#define E_SOURCE_EXTENSION_OAUTH2_CLIENT "OAuth2 Client"

G_BEGIN_DECLS

#define E_TYPE_SOURCE_OAUTH2_CLIENT e_source_oauth2_client_get_type ()

/**
 * ESourceOAuth2Client:
 * Since: 3.64
 **/
G_DECLARE_FINAL_TYPE (ESourceOAuth2Client, e_source_oauth2_client, E, SOURCE_OAUTH2_CLIENT, ESourceExtension)

const gchar *	e_source_oauth2_client_get_client_id
					(ESourceOAuth2Client *extension);
gchar *		e_source_oauth2_client_dup_client_id
					(ESourceOAuth2Client *extension);
void		e_source_oauth2_client_set_client_id
					(ESourceOAuth2Client *extension,
					 const gchar *client_id);
const gchar *	e_source_oauth2_client_get_client_secret
					(ESourceOAuth2Client *extension);
gchar *		e_source_oauth2_client_dup_client_secret
					(ESourceOAuth2Client *extension);
void		e_source_oauth2_client_set_client_secret
					(ESourceOAuth2Client *extension,
					 const gchar *client_secret);
const gchar *	e_source_oauth2_client_get_authorization_endpoint
					(ESourceOAuth2Client *extension);
gchar *		e_source_oauth2_client_dup_authorization_endpoint
					(ESourceOAuth2Client *extension);
void		e_source_oauth2_client_set_authorization_endpoint
					(ESourceOAuth2Client *extension,
					 const gchar *authorization_endpoint);
const gchar *	e_source_oauth2_client_get_token_endpoint
					(ESourceOAuth2Client *extension);
gchar *		e_source_oauth2_client_dup_token_endpoint
					(ESourceOAuth2Client *extension);
void		e_source_oauth2_client_set_token_endpoint
					(ESourceOAuth2Client *extension,
					 const gchar *token_endpoint);
const gchar *	e_source_oauth2_client_get_redirect_uri
					(ESourceOAuth2Client *extension);
gchar *		e_source_oauth2_client_dup_redirect_uri
					(ESourceOAuth2Client *extension);
void		e_source_oauth2_client_set_redirect_uri
					(ESourceOAuth2Client *extension,
					 const gchar *redirect_uri);
const gchar *	e_source_oauth2_client_get_scope
					(ESourceOAuth2Client *extension);
gchar *		e_source_oauth2_client_dup_scope
					(ESourceOAuth2Client *extension);
void		e_source_oauth2_client_set_scope
					(ESourceOAuth2Client *extension,
					 const gchar *scope);
const gchar *	e_source_oauth2_client_get_resource
					(ESourceOAuth2Client *extension);
gchar *		e_source_oauth2_client_dup_resource
					(ESourceOAuth2Client *extension);
void		e_source_oauth2_client_set_resource
					(ESourceOAuth2Client *extension,
					 const gchar *resource);
const gchar *	e_source_oauth2_client_get_issuer
					(ESourceOAuth2Client *extension);
gchar *		e_source_oauth2_client_dup_issuer
					(ESourceOAuth2Client *extension);
void		e_source_oauth2_client_set_issuer
					(ESourceOAuth2Client *extension,
					 const gchar *issuer);

G_END_DECLS

#endif /* E_SOURCE_OAUTH2_CLIENT_H */
