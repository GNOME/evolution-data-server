/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2013, Openismus GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Authors: Tristan Van Berkom <tristanvb@openismus.com>
 */

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>

#include "data-test-utils.h"

/* This forces the GType to be registered in a way that
 * avoids a "statement with no effect" compiler warning.
 * FIXME Use g_type_ensure() once we require GLib 2.34. */
#define REGISTER_TYPE(type) \
	(g_type_class_unref (g_type_class_ref (type)))


#define SQLITEDB_EMAIL_ID    "addressbook@localbackend.com"
#define SQLITEDB_FOLDER_NAME "folder"

gchar *
new_vcard_from_test_case (const gchar *case_name)
{
	gchar *filename;
	gchar *case_filename;
	GFile * file;
	GError *error = NULL;
	gchar *vcard;

	case_filename = g_strdup_printf ("%s.vcf", case_name);
	filename = g_build_filename (SRCDIR, "..", "libebook", "data", "vcards", case_filename, NULL);
	file = g_file_new_for_path (filename);
	if (!g_file_load_contents (file, NULL, &vcard, NULL, NULL, &error))
		g_error ("failed to read test contact file '%s': %s",
			 filename, error->message);

	g_free (case_filename);
	g_free (filename);
	g_object_unref (file);

	return vcard;
}

EContact *
new_contact_from_test_case (const gchar *case_name)
{
	gchar *vcard;
	EContact *contact = NULL;

	vcard = new_vcard_from_test_case (case_name);
	if (vcard)
		contact = e_contact_new_from_vcard (vcard);
	g_free (vcard);

	return contact;
}

static gboolean
contacts_are_equal_shallow (EContact *a,
                            EContact *b)
{
	const gchar *uid_a, *uid_b;

        /* Avoid warnings if one or more are NULL, to make this function
         * "NULL-friendly" */
	if (!a && !b)
		return TRUE;

	if (!E_IS_CONTACT (a) || !E_IS_CONTACT (b))
		return FALSE;

	uid_a = e_contact_get_const (a, E_CONTACT_UID);
	uid_b = e_contact_get_const (b, E_CONTACT_UID);

	return g_strcmp0 (uid_a, uid_b) == 0;
}

gboolean
add_contact_from_test_case_verify (EBookClient *book_client,
                                   const gchar *case_name,
                                   EContact **contact)
{
	EContact *contact_orig;
	EContact *contact_final;
	gchar *uid;
	GError *error = NULL;

	contact_orig = new_contact_from_test_case (case_name);

	if (!e_book_client_add_contact_sync (book_client, contact_orig, &uid, NULL, &error))
		g_error ("Failed to add contact: %s", error->message);

	e_contact_set (contact_orig, E_CONTACT_UID, uid);

	if (!e_book_client_get_contact_sync (book_client, uid, &contact_final, NULL, &error))
		g_error ("Failed to get contact: %s", error->message);

        /* verify the contact was added "successfully" (not thorough) */
	g_assert (contacts_are_equal_shallow (contact_orig, contact_final));

	if (contact)
                *contact = contact_final;
	else
		g_object_unref (contact_final);
	g_object_unref (contact_orig);
	g_free (uid);

	return TRUE;
}

static gchar *
get_addressbook_directory (ESourceRegistry *registry,
			   ESource         *source)
{
	ESource *builtin_source;
	const gchar *user_data_dir;
	const gchar *uid;
	gchar *filename = NULL;

	uid = e_source_get_uid (source);
	g_return_val_if_fail (uid != NULL, NULL);

	user_data_dir = e_get_user_data_dir ();

	builtin_source = e_source_registry_ref_builtin_address_book (registry);

	/* Special case directory for the builtin addressbook source */
	if (builtin_source != NULL && e_source_equal (source, builtin_source))
		uid = "system";

	filename = g_build_filename (user_data_dir, "addressbook", uid, NULL);

	if (builtin_source)
		g_object_unref (builtin_source);

	return filename;
}

static EBookBackendSqliteDB *
open_sqlitedb (ESourceRegistry *registry,
	       ESource         *source)
{
	EBookBackendSqliteDB *ebsdb;
	GError *error = NULL;
	gchar *dirname;

	dirname = get_addressbook_directory (registry, source);
	ebsdb   = e_book_backend_sqlitedb_new (dirname,
					       SQLITEDB_EMAIL_ID,
					       SQLITEDB_FOLDER_ID,
					       SQLITEDB_FOLDER_NAME,
					       TRUE, &error);

	if (!ebsdb)
		g_error ("Failed to open SQLite backend: %s", error->message);

	g_free (dirname);

	return ebsdb;
}

void
e_sqlitedb_fixture_setup (ESqliteDBFixture *fixture,
			  gconstpointer     user_data)
{
	EBookClient *book_client;

	e_test_server_utils_setup ((ETestServerFixture *)fixture, user_data);

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);
	fixture->ebsdb = open_sqlitedb (((ETestServerFixture *)fixture)->registry,
					e_client_get_source (E_CLIENT (book_client)));
}

void
e_sqlitedb_fixture_teardown (ESqliteDBFixture *fixture,
			     gconstpointer     user_data)
{
	g_object_unref (fixture->ebsdb);
	e_test_server_utils_teardown ((ETestServerFixture *)fixture, user_data);
}

void
e_sqlitedb_cursor_fixture_setup_book (ESource            *scratch,
				      ETestServerClosure *closure)
{
	ESourceBackendSummarySetup *setup;

	g_type_class_unref (g_type_class_ref (E_TYPE_SOURCE_BACKEND_SUMMARY_SETUP));
	setup = e_source_get_extension (scratch, E_SOURCE_EXTENSION_BACKEND_SUMMARY_SETUP);
	e_source_backend_summary_setup_set_summary_fields (setup,
							   E_CONTACT_FAMILY_NAME,
							   E_CONTACT_GIVEN_NAME,
							   E_CONTACT_EMAIL,
							   0);
	e_source_backend_summary_setup_set_indexed_fields (setup,
							   E_CONTACT_FAMILY_NAME, E_BOOK_INDEX_PREFIX,
							   E_CONTACT_GIVEN_NAME, E_BOOK_INDEX_PREFIX,
							   E_CONTACT_EMAIL, E_BOOK_INDEX_PREFIX,
							   0);
}

void
e_sqlitedb_cursor_fixture_setup (EbSdbCursorFixture *fixture,
				 gconstpointer       user_data)
{
	ETestServerFixture *base_fixture = (ETestServerFixture *)fixture;
	ESqliteDBFixture *ebsdb_fixture = (ESqliteDBFixture *)fixture;
	EbSdbCursorClosure *data = (EbSdbCursorClosure *)user_data;
	EContactField sort_fields[] = { E_CONTACT_FAMILY_NAME, E_CONTACT_GIVEN_NAME };
	EBookCursorSortType sort_types[] = { data->sort_type, data->sort_type };
	EBookClient *book_client;
	GSList *contacts = NULL;
	GError *error = NULL;
	gint i;
	gchar *sexp = NULL;
	const gchar *source_name;

	/* Support the migration tests */
	source_name = g_getenv ("MIGRATION_TEST_SOURCE_NAME");
	if (source_name != NULL)
		base_fixture->source_name = g_strdup (source_name);

	/* Setup the EBookClient, but don't open the EBookBackendSqliteDB until after
	 * we've specified the locale
	 */
	e_test_server_utils_setup ((ETestServerFixture *)fixture, user_data);
	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	if (data->locale)
		e_sqlitedb_cursor_fixture_set_locale (fixture, data->locale);
	else
		e_sqlitedb_cursor_fixture_set_locale (fixture, "en_US.UTF-8");

	/* Now open the EBookBackendSqliteDB */
	ebsdb_fixture->ebsdb = open_sqlitedb (((ETestServerFixture *)fixture)->registry,
					      e_client_get_source (E_CLIENT (book_client)));

	for (i = 0; i < N_SORTED_CONTACTS; i++) {
		gchar *case_name = g_strdup_printf ("sorted-%d", i + 1);
		gchar *vcard;
		EContact *contact;

		vcard    = new_vcard_from_test_case (case_name);
		contact  = e_contact_new_from_vcard (vcard);
		contacts = g_slist_prepend (contacts, contact);
		g_free (vcard);
		g_free (case_name);

		fixture->contacts[i] = contact;
	}

	if (!e_book_client_add_contacts_sync (book_client, contacts, NULL, NULL, &error)) { 

		/* Dont complain here, we re-use the same addressbook for multiple tests
		 * and we can't add the same contacts twice
		 */
		if (g_error_matches (error, E_BOOK_CLIENT_ERROR,
				     E_BOOK_CLIENT_ERROR_CONTACT_ID_ALREADY_EXISTS))
			g_clear_error (&error);
		else
			g_error ("Failed to add test contacts: %s", error->message);
	}

	g_slist_free (contacts);

	/* Allow a surrounding fixture setup to add a query here */
	if (fixture->query) {
		sexp = e_book_query_to_string (fixture->query);
		e_book_query_unref (fixture->query);
		fixture->query = NULL;
	}

	fixture->cursor = e_book_backend_sqlitedb_cursor_new (((ESqliteDBFixture *) fixture)->ebsdb,
							      SQLITEDB_FOLDER_ID,
							      sexp, sort_fields, sort_types, 2, &error);

	g_free (sexp);

	g_assert (fixture->cursor != NULL);
}

void
e_sqlitedb_cursor_fixture_filtered_setup (EbSdbCursorFixture *fixture,
					  gconstpointer  user_data)
{
	fixture->query = e_book_query_field_test (E_CONTACT_EMAIL, E_BOOK_QUERY_ENDS_WITH, ".com");

	e_sqlitedb_cursor_fixture_setup (fixture, user_data);
}

void
e_sqlitedb_cursor_fixture_teardown (EbSdbCursorFixture *fixture,
				    gconstpointer       user_data)
{
	gint i;

	for (i = 0; i < N_SORTED_CONTACTS; i++) {
		if (fixture->contacts[i])
			g_object_unref (fixture->contacts[i]);
	}

	if (fixture->locale1)
		g_object_unref (fixture->locale1);

	if (fixture->own_id > 0)
		g_bus_unown_name (fixture->own_id);

	e_book_backend_sqlitedb_cursor_free (((ESqliteDBFixture *) fixture)->ebsdb, fixture->cursor);
	e_sqlitedb_fixture_teardown ((ESqliteDBFixture *)fixture, user_data);
}

typedef struct {
	EbSdbCursorFixture *fixture;
	const gchar *locale;
} ChangeLocaleData;

static void
book_client_locale_change (EBookClient *book,
			   GParamSpec  *pspec,
			   ChangeLocaleData *data)
{
	ETestServerFixture *base_fixture = (ETestServerFixture *)data->fixture;

	if (!g_strcmp0 (e_book_client_get_locale (book), data->locale))
		g_main_loop_quit (base_fixture->loop);
}

void
e_sqlitedb_cursor_fixture_set_locale (EbSdbCursorFixture *fixture,
				      const gchar        *locale)
{
	ETestServerFixture *base_fixture = (ETestServerFixture *)fixture;
	EBookClient *book_client;
	ChangeLocaleData data = { fixture, locale };
	gulong handler_id;
	gchar *strv[2] = { NULL, NULL };

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	/* We're already in the right locale */
	if (g_strcmp0 (locale, e_book_client_get_locale (book_client)) == 0)
		return;

	if (!fixture->locale1) {
		GDBusConnection *bus;
		GError *error = NULL;

		/* We use the 'org.freedesktop.locale1 on the session bus instead
		 * of the system bus only for testing purposes... in real life
		 * this service is on the system bus.
		 */
		bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
		if (!bus)
			g_error ("Failed to get system bus: %s", error->message);

		fixture->locale1 = e_dbus_locale1_skeleton_new ();

		/* Set initial locale before exporting on the bus */
		strv[0] = g_strdup_printf ("LANG=%s", locale);
		e_dbus_locale1_set_locale (fixture->locale1, (const gchar * const *)strv);
		g_free (strv[0]);

		if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (fixture->locale1),
						       bus, "/org/freedesktop/locale1", &error))
			g_error ("Failed to export org.freedesktop.locale1: %s", error->message);

		fixture->own_id =
			g_bus_own_name_on_connection (bus,
						      "org.freedesktop.locale1",
						      G_BUS_NAME_OWNER_FLAGS_REPLACE,
						      NULL, NULL, NULL, NULL);

		g_object_unref (bus);
	} else {
		/* Send locale change message */
		strv[0] = g_strdup_printf ("LANG=%s", locale);
		e_dbus_locale1_set_locale (fixture->locale1, (const gchar * const *)strv);
		g_free (strv[0]);
	}

	handler_id = g_signal_connect (book_client, "notify::locale",
				       G_CALLBACK (book_client_locale_change), &data);
	g_main_loop_run (base_fixture->loop);
	g_signal_handler_disconnect (book_client, handler_id);
}

static gint
find_contact_data (EbSdbSearchData *data,
		   const gchar     *uid)
{
	return g_strcmp0 (data->uid, uid);
}

void
assert_contacts_order_slist (GSList      *results,
			     GSList      *uids)
{
	gint position = -1;
	GSList *link, *l;

	/* Assert that all passed UIDs are found in the
	 * results, and that those UIDs are in the
	 * specified order.
	 */
	for (l = uids; l; l = l->next) {
		const gchar *uid = l->data;
		gint new_position;

		link = g_slist_find_custom (results, uid, (GCompareFunc)find_contact_data);
		if (!link)
			g_error ("Specified uid '%s' was not found in results", uid);

		new_position = g_slist_position (results, link);
		g_assert_cmpint (new_position, >, position);
		position = new_position;
	}

}

void
assert_contacts_order (GSList      *results,
		       const gchar *first_uid,
		       ...)
{
	GSList *uids = NULL;
	gchar *uid;
	va_list args;

	g_assert (first_uid);

	uids = g_slist_append (uids, (gpointer)first_uid);

	va_start (args, first_uid);
	uid = va_arg (args, gchar*);
	while (uid) {
		uids = g_slist_append (uids, uid);
		uid = va_arg (args, gchar*);
	}
	va_end (args);

	assert_contacts_order_slist (results, uids);
	g_slist_free (uids);
}

void
print_results (GSList      *results)
{
	GSList *l;

	if (g_getenv ("TEST_DEBUG") == NULL)
		return;

	g_print ("\nPRINTING RESULTS:\n");

	for (l = results; l; l = l->next) {
		EbSdbSearchData *data = l->data;

		g_print ("\n%s\n", data->vcard);
	}

	g_print ("\nRESULT LIST_FINISHED\n");
}

/********************************************
 *           Move By Test Helpers
 ********************************************/
#define DEBUG_FIXTURE        0

static MoveByData *
move_by_test_new_internal (const gchar *test_path,
			   const gchar *locale,
			   gsize        struct_size)
{
	MoveByData *data;

	data = g_slice_alloc0 (struct_size);
	data->parent.parent.type = E_TEST_SERVER_ADDRESS_BOOK;
	data->parent.parent.customize = e_sqlitedb_cursor_fixture_setup_book;
	data->parent.locale = g_strdup (locale);
	data->parent.sort_type = E_BOOK_CURSOR_SORT_ASCENDING;
	data->path = g_strdup (test_path);
	data->struct_size = struct_size;

	/* Keep the work dir for migration tests */
	if (g_getenv ("MIGRATION_TEST_SOURCE_NAME") != NULL)
	    data->parent.parent.keep_work_directory = TRUE;

	return data;
}

static void
move_by_test_free (MoveByData *data)
{
	g_free (data->path);
	g_free ((gchar *)data->parent.locale);
	g_slice_free1 (data->struct_size, data);
}

MoveByData *
move_by_test_new (const gchar *test_path,
		  const gchar *locale)
{
	return move_by_test_new_internal (test_path, locale, sizeof (MoveByData));
}

MoveByData *
move_by_test_new_full (const gchar         *test_path,
		       const gchar         *locale,
		       EBookCursorSortType  sort_type)
{
	MoveByData *data;

	data = move_by_test_new_internal (test_path, locale, sizeof (MoveByData));
	data->parent.sort_type = sort_type;

	return data;
}

static void
test_cursor_move_teardown (EbSdbCursorFixture *fixture,
			   gconstpointer  user_data)
{
	MoveByData *data = (MoveByData *)user_data;

	e_sqlitedb_cursor_fixture_teardown (fixture, user_data);

	move_by_test_free (data);
}

static void
assert_move_by (EbSdbCursorFixture *fixture,
		MoveByData *data,
		gint i,
		GSList *results,
		gint n_results)
{
	GSList *uids = NULL;
	gint j, expected = 0;

	g_assert_cmpint (g_slist_length (results), ==, n_results);

	/* Count the number of really expected results */
	for (j = 0; j < ABS (data->counts[i]); j++) {
		gint index = data->expected[i][j];

		if (index < 0)
			break;

		expected++;
	}

	/* Assert the exact amount of requested results */
	g_assert_cmpint (g_slist_length (results), ==, expected);

#if DEBUG_FIXTURE
	g_print ("%s: Constructing expected result list for a fetch of %d: ",
		 data->path, data->counts[i]);
#endif
	for (j = 0; j < ABS (data->counts[i]); j++) {
		gint index = data->expected[i][j];
		gchar *uid;

		if (index < 0)
			break;

		uid = (gchar *)e_contact_get_const (fixture->contacts[index], E_CONTACT_UID);
		uids = g_slist_append (uids, uid);

#if DEBUG_FIXTURE
		g_print ("%s ", uid);
#endif

	}
#if DEBUG_FIXTURE
	g_print ("\n");
#endif

	assert_contacts_order_slist (results, uids);
	g_slist_free (uids);
}

static void
test_move_by (EbSdbCursorFixture *fixture,
	      gconstpointer  user_data)
{
	MoveByData *data = (MoveByData *)user_data;
	GSList *results = NULL;
	GError *error = NULL;
	gint i;
	gint expected_position = 0, position;
	gint total;
	gint n_results;

	total = data->filtered ? N_FILTERED_CONTACTS : N_SORTED_CONTACTS;

	for (i = 0; i < MAX_MOVE_BY_COUNTS && data->counts[i] != 0; i++) {

		/* From the 0 position, a negative move starts from the end */
		if (expected_position == 0 && data->counts[i] < 0)
			expected_position = (total + 1) - ABS (data->counts[i]);
		else
			expected_position += data->counts[i];

		if (expected_position > total || expected_position < 1)
			expected_position = 0;

		/* Try normal order */
		n_results = e_book_backend_sqlitedb_cursor_move_by (((ESqliteDBFixture *) fixture)->ebsdb,
								    fixture->cursor,
								    EBSDB_CURSOR_ORIGIN_CURRENT,
								    data->counts[i],
								    &results, &error);
		if (n_results < 0)
			g_error ("Error fetching cursor results: %s", error->message);

		print_results (results);
		assert_move_by (fixture, data, i, results, n_results);
		g_slist_foreach (results, (GFunc)e_book_backend_sqlitedb_search_data_free, NULL);
		g_slist_free (results);
		results = NULL;

		if (!e_book_backend_sqlitedb_cursor_calculate (((ESqliteDBFixture *) fixture)->ebsdb,
							       fixture->cursor, NULL, &position, &error))
			g_error ("Error calculating cursor: %s", error->message);
		g_assert_cmpint (expected_position, ==, position);

		/* Try repeat last query */
		n_results = e_book_backend_sqlitedb_cursor_move_by (((ESqliteDBFixture *) fixture)->ebsdb,
								    fixture->cursor,
								    EBSDB_CURSOR_ORIGIN_PREVIOUS,
								    data->counts[i],
								    &results, &error);
		if (n_results < 0)
			g_error ("Error fetching cursor results: %s", error->message);

		print_results (results);
		assert_move_by (fixture, data, i, results, n_results);
		g_slist_foreach (results, (GFunc)e_book_backend_sqlitedb_search_data_free, NULL);
		g_slist_free (results);
		results = NULL;

		if (!e_book_backend_sqlitedb_cursor_calculate (((ESqliteDBFixture *) fixture)->ebsdb,
							       fixture->cursor, NULL, &position, &error))
			g_error ("Error calculating cursor: %s", error->message);
		g_assert_cmpint (expected_position, ==, position);
	}

	/* One more, test reset API, the first batch from the beginning */
	n_results = e_book_backend_sqlitedb_cursor_move_by (((ESqliteDBFixture *) fixture)->ebsdb,
							    fixture->cursor,
							    EBSDB_CURSOR_ORIGIN_RESET,
							    data->counts[0],
							    &results, &error);
	if (n_results < 0)
		g_error ("Error fetching cursor results: %s", error->message);

	print_results (results);
	assert_move_by (fixture, data, 0, results, n_results);
	g_slist_foreach (results, (GFunc)e_book_backend_sqlitedb_search_data_free, NULL);
	g_slist_free (results);
	results = NULL;

	/* From the 0 position, a negative move starts from the end */
	if (data->counts[0] < 0)
		expected_position = (total + 1) - ABS (data->counts[0]);
	else
		expected_position = data->counts[0];

	if (!e_book_backend_sqlitedb_cursor_calculate (((ESqliteDBFixture *) fixture)->ebsdb,
						       fixture->cursor, NULL, &position, &error))
		g_error ("Error calculating cursor: %s", error->message);
	g_assert_cmpint (expected_position, ==, position);
}

static void
move_by_test_add_assertion_va_list (MoveByData *data,
				    gint        count,
				    va_list     args)
{
	gint i, j;
	gint expected;

	for (i = 0; i < MAX_MOVE_BY_COUNTS; i++) {

		/* Find the next available test slot */
		if (data->counts[i] == 0) {
			data->counts[i] = count;

#if DEBUG_FIXTURE
			g_print ("Adding assertion to test %d: %s\n", i + 1, data->path);
			g_print ("  Test will move by %d and expect: ", count);
#endif
			for (j = 0; j < ABS (count); j++) {
				expected = va_arg (args, gint);

#if DEBUG_FIXTURE
				g_print ("%d ", expected);
#endif
				data->expected[i][j] = expected - 1;
			}
#if DEBUG_FIXTURE
			g_print ("\n");
#endif

			break;
		}
	}

	g_assert (i < MAX_MOVE_BY_COUNTS);
}

/* A positive of negative 'count' value
 * followed by ABS (count) UID indexes.
 *
 * The indexes start at 1 so that they
 * are easier to match up with the chart
 * in data-test-utils.h
 */
void
move_by_test_add_assertion (MoveByData *data,
			    gint        count,
			    ...)
{

	va_list args;

	va_start (args, count);
	move_by_test_add_assertion_va_list (data, count, args);
	va_end (args);
}

void
move_by_test_add (MoveByData  *data,
		  gboolean     filtered)
{
	data->filtered = filtered;

	g_test_add (data->path, EbSdbCursorFixture, data,
		    filtered ?
		    e_sqlitedb_cursor_fixture_filtered_setup :
		    e_sqlitedb_cursor_fixture_setup,
		    test_move_by,
		    test_cursor_move_teardown);
}

