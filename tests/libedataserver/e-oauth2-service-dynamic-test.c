/*
 * SPDX-FileCopyrightText: (C) 2026 Tobias Mueller <muelli@cryptobitch.de>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "evolution-data-server-config.h"

#include <string.h>

#include <camel/camel.h>
#include <libedataserver/libedataserver.h>

static ESource *
build_source (const gchar *uid,
	      gboolean with_client)
{
	ESource *source;
	ESourceAuthentication *auth;
	ESourceOAuth2Client *client;
	GError *local_error = NULL;

	source = e_source_new_with_uid (uid, NULL, &local_error);
	g_assert_no_error (local_error);

	auth = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);
	e_source_authentication_set_method (auth, "OAuth2Dynamic");

	if (with_client) {
		client = e_source_get_extension (source, E_SOURCE_EXTENSION_OAUTH2_CLIENT);
		e_source_oauth2_client_set_client_id (client, "client-abc123");
		e_source_oauth2_client_set_authorization_endpoint (client, "https://auth.example.com/authorize");
		e_source_oauth2_client_set_token_endpoint (client, "https://auth.example.com/token");
		e_source_oauth2_client_set_scope (client, "mail offline_access");
		e_source_oauth2_client_set_resource (client, "https://api.example.com/session");
	}

	return source;
}

static void
test_selected_by_method (void)
{
	EOAuth2Services *services;
	EOAuth2Service *service;
	ESource *source;

	services = e_oauth2_services_new ();

	/* Selected by the authentication method, like every service; the
	   client data comes from the extension, NULL when it is absent. */
	source = build_source ("dynamic-selected-bare", FALSE);
	service = e_oauth2_services_find (services, source);
	g_assert_nonnull (service);
	g_assert_cmpstr (e_oauth2_service_get_name (service), ==, "OAuth2Dynamic");
	g_assert_null (e_oauth2_service_get_client_id (service, source));
	g_clear_object (&service);
	g_object_unref (source);

	source = build_source ("dynamic-selected", TRUE);
	service = e_oauth2_services_find (services, source);
	g_assert_nonnull (service);
	g_assert_cmpstr (e_oauth2_service_get_client_id (service, source), ==, "client-abc123");
	g_clear_object (&service);
	g_object_unref (source);

	g_object_unref (services);
}

static void
test_sasl_mechanism_is_xoauth2 (void)
{
	CamelServiceAuthType *authtype;

	authtype = camel_sasl_authtype ("OAuth2Dynamic");
	g_assert_nonnull (authtype);
	g_assert_cmpstr (authtype->authproto, ==, "OAuth2Dynamic");
	g_assert_true (camel_sasl_is_xoauth2_alias ("OAuth2Dynamic"));
}

static void
test_client_id_follows_a_live_change (void)
{
	EOAuth2Services *services;
	EOAuth2Service *service;
	ESource *source;
	ESourceOAuth2Client *client;

	services = e_oauth2_services_new ();
	source = build_source ("dynamic-live-change", TRUE);
	service = e_oauth2_services_find (services, source);
	g_assert_nonnull (service);

	g_assert_cmpstr (e_oauth2_service_get_client_id (service, source), ==, "client-abc123");

	/* The service must not keep returning the value it read first */
	client = e_source_get_extension (source, E_SOURCE_EXTENSION_OAUTH2_CLIENT);
	e_source_oauth2_client_set_client_id (client, "client-def456");

	g_assert_cmpstr (e_oauth2_service_get_client_id (service, source), ==, "client-def456");

	g_clear_object (&service);
	g_object_unref (source);
	g_object_unref (services);
}

static void
test_authentication_uri_query (void)
{
	EOAuth2Services *services;
	EOAuth2Service *service;
	ESource *source;
	GHashTable *query;
	const gchar *challenge;

	services = e_oauth2_services_new ();
	source = build_source ("dynamic-uri-query", TRUE);
	service = e_oauth2_services_find (services, source);
	g_assert_nonnull (service);

	query = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	e_oauth2_service_prepare_authentication_uri_query (service, source, query);

	/* The default query prep still ran. */
	g_assert_cmpstr (g_hash_table_lookup (query, "response_type"), ==, "code");
	/* Our additions from the client extension. */
	g_assert_cmpstr (g_hash_table_lookup (query, "scope"), ==, "mail offline_access");
	g_assert_cmpstr (g_hash_table_lookup (query, "resource"), ==, "https://api.example.com/session");
	/* PKCE S256, 43-char base64url-encoded SHA-256 with no padding. */
	g_assert_cmpstr (g_hash_table_lookup (query, "code_challenge_method"), ==, "S256");
	challenge = g_hash_table_lookup (query, "code_challenge");
	g_assert_nonnull (challenge);
	g_assert_cmpuint (strlen (challenge), ==, 43);
	g_assert_null (strchr (challenge, '='));
	g_assert_null (strchr (challenge, '+'));
	g_assert_null (strchr (challenge, '/'));

	g_hash_table_destroy (query);
	g_clear_object (&service);
	g_object_unref (source);
	g_object_unref (services);
}

static void
test_second_uri_reuses_the_pending_challenge (void)
{
	EOAuth2Services *services;
	EOAuth2Service *service;
	ESource *source;
	GHashTable *first_query, *second_query, *form;
	const gchar *first_challenge, *second_challenge;

	services = e_oauth2_services_new ();
	source = build_source ("dynamic-second-uri", TRUE);
	service = e_oauth2_services_find (services, source);
	g_assert_nonnull (service);

	/* Both URIs must carry the challenge of the one pending verifier */
	first_query = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	e_oauth2_service_prepare_authentication_uri_query (service, source, first_query);
	first_challenge = g_hash_table_lookup (first_query, "code_challenge");

	second_query = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	e_oauth2_service_prepare_authentication_uri_query (service, source, second_query);
	second_challenge = g_hash_table_lookup (second_query, "code_challenge");

	g_assert_nonnull (first_challenge);
	g_assert_cmpstr (first_challenge, ==, second_challenge);

	form = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	e_oauth2_service_prepare_get_token_form (service, source, "the-code", form);
	g_assert_nonnull (g_hash_table_lookup (form, "code_verifier"));

	g_hash_table_destroy (first_query);
	g_hash_table_destroy (second_query);
	g_hash_table_destroy (form);
	g_clear_object (&service);
	g_object_unref (source);
	g_object_unref (services);
}

static void
test_token_form_redeems_verifier_once (void)
{
	EOAuth2Services *services;
	EOAuth2Service *service;
	ESource *source;
	GHashTable *query, *form;

	services = e_oauth2_services_new ();
	source = build_source ("dynamic-redeem", TRUE);
	service = e_oauth2_services_find (services, source);
	g_assert_nonnull (service);

	query = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	e_oauth2_service_prepare_authentication_uri_query (service, source, query);
	g_hash_table_destroy (query);

	/* First token exchange redeems the stashed verifier. */
	form = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	e_oauth2_service_prepare_get_token_form (service, source, "the-code", form);
	g_assert_nonnull (g_hash_table_lookup (form, "code_verifier"));
	g_assert_cmpstr (g_hash_table_lookup (form, "resource"), ==, "https://api.example.com/session");
	g_hash_table_destroy (form);

	/* A second exchange without a fresh challenge has no verifier to send. */
	form = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	e_oauth2_service_prepare_get_token_form (service, source, "the-code", form);
	g_assert_null (g_hash_table_lookup (form, "code_verifier"));
	g_hash_table_destroy (form);

	g_clear_object (&service);
	g_object_unref (source);
	g_object_unref (services);
}

static ESource *
ref_no_source (gpointer user_data,
	       const gchar *uid)
{
	return NULL;
}

static void
test_missing_token_endpoint_is_an_error (void)
{
	EOAuth2Services *services;
	EOAuth2Service *service;
	ESource *source;
	GError *local_error = NULL;

	services = e_oauth2_services_new ();
	source = build_source ("dynamic-no-endpoint", FALSE);
	service = e_oauth2_services_find (services, source);
	g_assert_nonnull (service);

	g_assert_false (e_oauth2_service_receive_and_store_token_sync (service, source, "the-code",
		ref_no_source, NULL, NULL, &local_error));
	g_assert_error (local_error, E_OAUTH2_SERVICE_ERROR, E_OAUTH2_SERVICE_ERROR_FAILED);
	g_clear_error (&local_error);

	g_assert_false (e_oauth2_service_refresh_and_store_token_sync (service, source, "the-refresh-token",
		ref_no_source, NULL, NULL, &local_error));
	g_assert_error (local_error, E_OAUTH2_SERVICE_ERROR, E_OAUTH2_SERVICE_ERROR_FAILED);
	g_clear_error (&local_error);

	g_clear_object (&service);
	g_object_unref (source);
	g_object_unref (services);
}

gint
main (gint argc,
      gchar *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/e-oauth2-service-dynamic/SelectedByMethod", test_selected_by_method);
	g_test_add_func ("/e-oauth2-service-dynamic/SaslMechanismIsXOAuth2", test_sasl_mechanism_is_xoauth2);
	g_test_add_func ("/e-oauth2-service-dynamic/ClientIdFollowsALiveChange", test_client_id_follows_a_live_change);
	g_test_add_func ("/e-oauth2-service-dynamic/AuthenticationUriQuery", test_authentication_uri_query);
	g_test_add_func ("/e-oauth2-service-dynamic/SecondUriReusesThePendingChallenge", test_second_uri_reuses_the_pending_challenge);
	g_test_add_func ("/e-oauth2-service-dynamic/TokenFormRedeemsVerifierOnce", test_token_form_redeems_verifier_once);
	g_test_add_func ("/e-oauth2-service-dynamic/MissingTokenEndpointIsAnError", test_missing_token_endpoint_is_an_error);

	return g_test_run ();
}
