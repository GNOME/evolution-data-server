/*
 * e-source-registry-test.c
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

#include "e-test-server-utils.h"

#include <libedataserver/libedataserver.h>

static ETestServiceType test_closure = {
	E_TEST_SERVER_NONE, NULL, 0, FALSE };

static gboolean
test_commit_source_idle_cb (gpointer user_data)
{
	ETestServerFixture *fixture = user_data;
	ESource *source;
	gchar *uid;
	GError *error = NULL;

	/* Configure a minimal scratch source. */
	source = e_source_new (NULL, NULL, &error);
	g_assert_no_error (error);
	e_source_set_parent (source, "local-stub");
	e_source_set_display_name (source, "Test Commit Source");

	/* Note the ESource UID. */
	uid = e_source_dup_uid (source);

	/* Submit the scratch source. */
	e_source_registry_commit_source_sync (
		fixture->registry, source, NULL, &error);
	g_assert_no_error (error);

	/* Discard the scratch source. */
	g_object_unref (source);

	/* Verify the registry has an ESource with the same UID. */
	source = e_source_registry_ref_source (fixture->registry, uid);

	g_free (uid);

	if (source != NULL) {
		g_object_unref (source);
	} else {
		g_test_fail ();
	}

	g_main_loop_quit (fixture->loop);

	return FALSE;
}

static void
test_commit_source (ETestServerFixture *fixture,
                    gconstpointer user_data)
{
	g_test_bug ("685986");
	g_idle_add (test_commit_source_idle_cb, fixture);
	g_main_loop_run (fixture->loop);
}

gint
main (gint argc,
      gchar **argv)
{
	gint retval;

	g_type_init ();

	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add (
		"/e-source-registry-test/CommitSource",
		ETestServerFixture, &test_closure,
		e_test_server_utils_setup,
		test_commit_source,
		e_test_server_utils_teardown);

	retval = e_test_server_utils_run ();

	/* XXX Something is leaking a GDBusConnection reference.
	 *     Leave this disabled until I can track it down. */
	/* g_object_unref (closure.test_dbus); */

	return retval;
}
