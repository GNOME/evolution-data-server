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

static void
test_address_book_property_roundtrip (void)
{
	const gchar * const groups1[] = { "Friends", "Family", NULL };
	const gchar * const groups2[] = { "Work", NULL };
	ESource *source;
	ESourceAddressBook *extension;
	gchar **dup_val = NULL;
	gchar **prop_val = NULL;
	GError *local_error = NULL;

	source = e_source_new_with_uid ("address-book-test", NULL, &local_error);
	g_assert_no_error (local_error);

	g_assert_false (e_source_has_extension (source, E_SOURCE_EXTENSION_ADDRESS_BOOK));

	extension = e_source_get_extension (source, E_SOURCE_EXTENSION_ADDRESS_BOOK);
	g_assert_nonnull (extension);

	/* Verify initially empty/NULL. */
	g_assert_null (e_source_address_book_get_groups (extension));
	dup_val = e_source_address_book_dup_groups (extension);
	g_assert_null (dup_val);
	g_object_get (extension, "groups", &prop_val, NULL);
	g_assert_null (prop_val);

	/* Set groups via setter. */
	e_source_address_book_set_groups (extension, groups1);
	g_assert_cmpstrv (e_source_address_book_get_groups (extension), groups1);

	dup_val = e_source_address_book_dup_groups (extension);
	g_assert_cmpstrv (dup_val, groups1);
	g_strfreev (dup_val);

	g_object_get (extension, "groups", &prop_val, NULL);
	g_assert_cmpstrv (prop_val, groups1);
	g_strfreev (prop_val);

	/* Set groups via g_object_set. */
	g_object_set (extension, "groups", groups2, NULL);
	g_assert_cmpstrv (e_source_address_book_get_groups (extension), groups2);

	dup_val = e_source_address_book_dup_groups (extension);
	g_assert_cmpstrv (dup_val, groups2);
	g_strfreev (dup_val);

	/* Clear groups with NULL. */
	e_source_address_book_set_groups (extension, NULL);
	g_assert_null (e_source_address_book_get_groups (extension));

	/* Also test order property */
	g_assert_cmpuint (e_source_address_book_get_order (extension), ==, 0);
	e_source_address_book_set_order (extension, 5);
	g_assert_cmpuint (e_source_address_book_get_order (extension), ==, 5);

	g_object_unref (source);
}

static void
test_address_book_keyfile_group (void)
{
	const gchar * const groups[] = { "Friends", "Family", NULL };
	ESource *source;
	ESourceAddressBook *extension;
	gchar *data;
	GError *local_error = NULL;

	source = e_source_new_with_uid ("address-book-test", NULL, &local_error);
	g_assert_no_error (local_error);

	extension = e_source_get_extension (source, E_SOURCE_EXTENSION_ADDRESS_BOOK);
	e_source_address_book_set_groups (extension, groups);
	e_source_address_book_set_order (extension, 3);

	data = e_source_to_string (source, NULL);
	g_assert_nonnull (data);
	g_assert_nonnull (strstr (data, "[Address Book]"));
	g_assert_nonnull (strstr (data, "Groups=Friends;Family;"));
	g_assert_nonnull (strstr (data, "Order=3"));
	g_free (data);

	g_object_unref (source);
}

static void
test_selectable_property_roundtrip (void)
{
	const gchar * const groups1[] = { "Work", "Private", NULL };
	const gchar * const groups2[] = { "Shared", NULL };
	ESource *source;
	ESourceSelectable *extension;
	gchar **dup_val = NULL;
	gchar **prop_val = NULL;
	GError *local_error = NULL;

	source = e_source_new_with_uid ("selectable-test", NULL, &local_error);
	g_assert_no_error (local_error);

	/* ESourceSelectable is abstract; use concrete ESourceCalendar extension. */
	g_assert_false (e_source_has_extension (source, E_SOURCE_EXTENSION_CALENDAR));

	extension = E_SOURCE_SELECTABLE (e_source_get_extension (source, E_SOURCE_EXTENSION_CALENDAR));
	g_assert_nonnull (extension);

	/* Verify initially empty/NULL. */
	g_assert_null (e_source_selectable_get_groups (extension));
	dup_val = e_source_selectable_dup_groups (extension);
	g_assert_null (dup_val);
	g_object_get (extension, "groups", &prop_val, NULL);
	g_assert_null (prop_val);

	/* Set groups via setter. */
	e_source_selectable_set_groups (extension, groups1);
	g_assert_cmpstrv (e_source_selectable_get_groups (extension), groups1);

	dup_val = e_source_selectable_dup_groups (extension);
	g_assert_cmpstrv (dup_val, groups1);
	g_strfreev (dup_val);

	g_object_get (extension, "groups", &prop_val, NULL);
	g_assert_cmpstrv (prop_val, groups1);
	g_strfreev (prop_val);

	/* Set groups via g_object_set. */
	g_object_set (extension, "groups", groups2, NULL);
	g_assert_cmpstrv (e_source_selectable_get_groups (extension), groups2);

	dup_val = e_source_selectable_dup_groups (extension);
	g_assert_cmpstrv (dup_val, groups2);
	g_strfreev (dup_val);

	/* Clear groups with NULL. */
	e_source_selectable_set_groups (extension, NULL);
	g_assert_null (e_source_selectable_get_groups (extension));

	g_object_unref (source);
}

static void
test_selectable_keyfile_group (void)
{
	const gchar * const groups[] = { "Work", "Private", NULL };
	ESource *source;
	ESourceSelectable *extension;
	gchar *data;
	GError *local_error = NULL;

	source = e_source_new_with_uid ("selectable-test", NULL, &local_error);
	g_assert_no_error (local_error);

	extension = E_SOURCE_SELECTABLE (e_source_get_extension (source, E_SOURCE_EXTENSION_CALENDAR));
	e_source_selectable_set_groups (extension, groups);

	data = e_source_to_string (source, NULL);
	g_assert_nonnull (data);
	g_assert_nonnull (strstr (data, "[Calendar]"));
	g_assert_nonnull (strstr (data, "Groups=Work;Private;"));
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
	g_test_add_func ("/e-source-address-book/PropertyRoundtrip", test_address_book_property_roundtrip);
	g_test_add_func ("/e-source-address-book/KeyfileGroup", test_address_book_keyfile_group);
	g_test_add_func ("/e-source-selectable/PropertyRoundtrip", test_selectable_property_roundtrip);
	g_test_add_func ("/e-source-selectable/KeyfileGroup", test_selectable_keyfile_group);

	return g_test_run ();
}
