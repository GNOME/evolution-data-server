/*
 * SPDX-FileCopyrightText: (C) 2026 Tobias Mueller <muelli@cryptobitch.de>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "evolution-data-server-config.h"

#include <string.h>

#include <libedataserver/libedataserver.h>

static void
test_oauth2_client_property_roundtrip (void)
{
	const gchar *names[] = {
		"client-id", "client-secret", "authorization-endpoint", "token-endpoint",
		"redirect-uri", "scope", "resource", "issuer"
	};
	ESource *source;
	ESourceOAuth2Client *extension;
	gchar *value;
	guint ii;
	GError *local_error = NULL;

	source = e_source_new_with_uid ("oauth2-client-test", NULL, &local_error);
	g_assert_no_error (local_error);

	g_assert_false (e_source_has_extension (source, E_SOURCE_EXTENSION_OAUTH2_CLIENT));

	extension = e_source_get_extension (source, E_SOURCE_EXTENSION_OAUTH2_CLIENT);
	g_assert_nonnull (extension);

	for (ii = 0; ii < G_N_ELEMENTS (names); ii++) {
		gchar *expected = g_strconcat ("value of ", names[ii], NULL);

		g_object_set (extension, names[ii], expected, NULL);
		g_object_get (extension, names[ii], &value, NULL);
		g_assert_cmpstr (value, ==, expected);

		g_free (expected);
		g_free (value);
	}

	g_assert_cmpstr (e_source_oauth2_client_get_client_id (extension), ==, "value of client-id");
	g_assert_cmpstr (e_source_oauth2_client_get_issuer (extension), ==, "value of issuer");

	/* Whitespace strips; an empty value reads back as NULL. */
	g_object_set (extension, "scope", "  mail  ", "client-secret", "", NULL);
	g_object_get (extension, "scope", &value, NULL);
	g_assert_cmpstr (value, ==, "mail");
	g_free (value);
	g_object_get (extension, "client-secret", &value, NULL);
	g_assert_null (value);

	g_object_unref (source);
}

static void
test_oauth2_client_keyfile_group (void)
{
	ESource *source;
	ESourceOAuth2Client *extension;
	gchar *data;
	GError *local_error = NULL;

	source = e_source_new_with_uid ("oauth2-client-test", NULL, &local_error);
	g_assert_no_error (local_error);

	extension = e_source_get_extension (source, E_SOURCE_EXTENSION_OAUTH2_CLIENT);
	e_source_oauth2_client_set_client_id (extension, "client-abc123");
	e_source_oauth2_client_set_issuer (extension, "https://auth.example.com");

	data = e_source_to_string (source, NULL);
	g_assert_nonnull (data);
	g_assert_nonnull (strstr (data, "[OAuth2 Client]"));
	g_assert_nonnull (strstr (data, "ClientId=client-abc123"));
	g_assert_nonnull (strstr (data, "Issuer=https://auth.example.com"));
	g_free (data);

	g_object_unref (source);
}

gint
main (gint argc,
      gchar *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/e-source-oauth2-client/PropertyRoundtrip", test_oauth2_client_property_roundtrip);
	g_test_add_func ("/e-source-oauth2-client/KeyfileGroup", test_oauth2_client_keyfile_group);

	return g_test_run ();
}
