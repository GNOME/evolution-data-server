/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2013 Intel Corporation
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Tristan Van Berkom <tristanvb@openismus.com>
 */

#include "evolution-data-server-config.h"

#include <stdlib.h>
#include <libebook/libebook.h>

#include "client-test-utils.h"
#include "e-test-server-utils.h"

#ifdef ENABLE_PHONENUMBER

typedef struct {
	ETestServerClosure parent;

	gchar *vcard_name;
	gchar *formatted_number;
	gchar *country_calling_code;
	gchar *national_number;
} TestData;

static void
test_data_free (gpointer user_data)
{
	TestData *const data = user_data;

	g_free (data->vcard_name);
	g_free (data->formatted_number);
	g_free (data->country_calling_code);
	g_free (data->national_number);
	g_free (data);
}

static TestData *
test_data_new (const gchar *vcard_name,
               const gchar *formatted_number,
               const gchar *country_calling_code,
               const gchar *national_number,
               gboolean direct)
{
	TestData *const data = g_new0 (TestData, 1);

	data->parent.type = direct ? E_TEST_SERVER_DIRECT_ADDRESS_BOOK : E_TEST_SERVER_ADDRESS_BOOK;
	data->parent.destroy_closure_func = test_data_free;
	data->vcard_name = g_strdup (vcard_name);
	data->formatted_number = g_strdup (formatted_number);
	data->country_calling_code = g_strdup (country_calling_code);
	data->national_number = g_strdup (national_number);

	g_print ("%d %p\n", data->parent.calendar_source_type, data->parent.destroy_closure_func);

	return data;
}

static void
test_add_e164_param (ETestServerFixture *fixture,
                     gconstpointer user_data)
{
	const TestData *const data = user_data;
	EBookClient          *book_client;
	EContact             *contact;
	gchar                *vcard;
	gchar                *uid;
	EVCardAttribute      *tel;
	GList                *values;
	GError               *error = NULL;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);
	g_print ("%p\n", book_client);

	vcard = new_vcard_from_test_case (data->vcard_name);
	contact = e_contact_new_from_vcard (vcard);
	g_free (vcard);

	tel = e_vcard_get_attribute (E_VCARD (contact), EVC_TEL);
	values = tel ? e_vcard_attribute_get_values (tel) : NULL;

	g_assert (values != NULL);
	g_assert_cmpstr (values->data, ==, data->formatted_number);

	values = e_vcard_attribute_get_param (tel, EVC_X_E164);
	g_assert (values == NULL);

	if (!e_book_client_add_contact_sync (book_client, contact, E_BOOK_OPERATION_FLAG_NONE, &uid, NULL, &error))
		g_error ("Failed to add contact: %s", error->message);

	g_object_unref (contact);

	if (!e_book_client_get_contact_sync (book_client, uid, &contact, NULL, &error))
		g_error ("Failed to restore contact: %s", error->message);

	g_free (uid);

	tel = e_vcard_get_attribute (E_VCARD (contact), EVC_TEL);
	values = tel ? e_vcard_attribute_get_values (tel) : NULL;

	g_assert (values != NULL);
	g_assert_cmpstr (values->data, ==, data->formatted_number);

	values = e_vcard_attribute_get_param (tel, EVC_X_E164);

	g_assert (values != NULL);
	g_assert_cmpstr (values->data, ==, data->national_number);

	if (data->country_calling_code) {
		g_assert (values->next != NULL);
		g_assert_cmpstr (values->next->data, ==, data->country_calling_code);
	} else {
		g_assert (values->next == NULL);
	}
}

#endif /* ENABLE_PHONENUMBER */

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/");

	client_test_utils_read_args (argc, argv);

#ifdef ENABLE_PHONENUMBER

	g_test_add (
		"/EBookClient/AddContact/AddE164Param/1",
		ETestServerFixture,
		test_data_new (
			"custom-1",
			"+1-221-5423789",
			"+1", "2215423789",
			FALSE),
		e_test_server_utils_setup,
		test_add_e164_param,
		e_test_server_utils_teardown);
	g_test_add (
		"/EBookClient/AddContact/AddE164Param/2",
		ETestServerFixture,
		test_data_new (
			"custom-2",
			"7654321",
			NULL, "7654321",
			FALSE),
		e_test_server_utils_setup,
		test_add_e164_param,
		e_test_server_utils_teardown);
	g_test_add (
		"/EBookClient/DirectAccess/AddContact/AddE164Param/1",
		ETestServerFixture,
		test_data_new (
			"custom-1",
			"+1-221-5423789",
			"+1", "2215423789",
			TRUE),
		e_test_server_utils_setup,
		test_add_e164_param,
		e_test_server_utils_teardown);
	g_test_add (
		"/EBookClient/DirectAccess/AddContact/AddE164Param/2",
		ETestServerFixture,
		test_data_new (
			"custom-2",
			"7654321",
			NULL, "7654321",
			TRUE),
		e_test_server_utils_setup,
		test_add_e164_param,
		e_test_server_utils_teardown);

#endif /* ENABLE_PHONENUMBER */

	return e_test_server_utils_run (argc, argv);
}
