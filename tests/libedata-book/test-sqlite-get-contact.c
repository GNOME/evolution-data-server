/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
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
 */

#include <stdlib.h>
#include <locale.h>
#include <libebook/libebook.h>

#include "e-test-server-utils.h"
#include "data-test-utils.h"

static void
test_get_contact (EbSqlFixture *fixture,
                  gconstpointer user_data)
{
	EContact *contact = NULL;
	EContact *other = NULL;
	GError *error = NULL;

	add_contact_from_test_case (fixture, "simple-1", &contact);

	if (!e_book_sqlite_get_contact (fixture->ebsql,
					(const gchar *) e_contact_get_const (contact, E_CONTACT_UID),
					FALSE,
					&other,
					&error))
		g_error (
			"Failed to get contact with uid '%s': %s",
			(const gchar *) e_contact_get_const (contact, E_CONTACT_UID),
			error->message);

	g_object_unref (contact);
	g_object_unref (other);
}

static EbSqlClosure closures[] = {
	{ FALSE, NULL },
	{ TRUE, NULL },
	{ FALSE, setup_empty_book },
	{ TRUE, setup_empty_book }
};

static const gchar *paths[] = {
	"/EBookSqlite/DefaultSummary/StoreVCards/GetContact",
	"/EBookSqlite/DefaultSummary/NoVCards/GetContact",
	"/EBookSqlite/EmptySummary/StoreVCards/GetContact",
	"/EBookSqlite/EmptrySummary/NoVCards/GetContact"
};

gint
main (gint argc,
      gchar **argv)
{
	gint i;

#if !GLIB_CHECK_VERSION (2, 35, 1)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);

	/* Ensure that the client and server get the same locale */
	g_assert (g_setenv ("LC_ALL", "en_US.UTF-8", TRUE));
	setlocale (LC_ALL, "");

	for (i = 0; i < G_N_ELEMENTS (closures); i++)
		g_test_add (
			paths[i], EbSqlFixture, &closures[i],
			e_sqlite_fixture_setup, test_get_contact, e_sqlite_fixture_teardown);

	return e_test_server_utils_run_full (0);
}
