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

#include "test-book-cache-utils.h"

static void
test_get_contact (TCUFixture *fixture,
		  gconstpointer user_data)
{
	EContact *contact = NULL;
	EContact *other = NULL;
	GError *error = NULL;

	tcu_add_contact_from_test_case (fixture, "simple-1", &contact);

	if (!e_book_cache_get_contact (fixture->book_cache,
		(const gchar *) e_contact_get_const (contact, E_CONTACT_UID),
		FALSE, &other, NULL, &error)) {
		g_error (
			"Failed to get contact with uid '%s': %s",
			(const gchar *) e_contact_get_const (contact, E_CONTACT_UID),
			error->message);
	}

	g_object_unref (contact);
	g_object_unref (other);
}

static TCUClosure closures[] = {
	{ NULL },
	{ tcu_setup_empty_book }
};

static const gchar *paths[] = {
	"/EBookCache/DefaultSummary/GetContact",
	"/EBookCache/EmptySummary/GetContact",
};

gint
main (gint argc,
      gchar **argv)
{
	gint ii;

#if !GLIB_CHECK_VERSION (2, 35, 1)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);

	/* Ensure that the client and server get the same locale */
	g_assert (g_setenv ("LC_ALL", "en_US.UTF-8", TRUE));
	setlocale (LC_ALL, "");

	for (ii = 0; ii < G_N_ELEMENTS (closures); ii++) {
		g_test_add (
			paths[ii], TCUFixture, &closures[ii],
			tcu_fixture_setup, test_get_contact, tcu_fixture_teardown);
	}

	return g_test_run ();
}
