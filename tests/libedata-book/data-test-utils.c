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
	gchar *vcard;
	EContact *contact_orig;
	EContact *contact_final;
	gchar *uid;
	GError *error = NULL;

	vcard = new_vcard_from_test_case (case_name);
	contact_orig = e_contact_new_from_vcard (vcard);
	g_free (vcard);
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
	GError *error;
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


static gint
find_contact_data (EbSdbSearchData *data,
		   const gchar     *uid)
{
	return g_strcmp0 (data->uid, uid);
}

void
assert_contacts_order (GSList      *results,
		       const gchar *first_uid,
		       ...)
{
	GSList *uids = NULL, *link, *l;
	gint position = -1;
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

	/* Assert that all passed UIDs are found in the
	 * results, and that those UIDs are in the
	 * specified order.
	 */
	for (l = uids; l; l = l->next) {
		gint new_position;

		uid = l->data;

		link = g_slist_find_custom (results, uid, (GCompareFunc)find_contact_data);
		if (!link)
			g_error ("Specified uid was not found in results");

		new_position = g_slist_position (results, link);
		g_assert_cmpint (new_position, >, position);
		position = new_position;
	}

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
