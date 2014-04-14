/*
 * e-source-registry-test.c
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
 */

#include "e-test-server-utils.h"

#include <libedataserver/libedataserver.h>

static ETestServerClosure test_closure = { E_TEST_SERVER_NONE, NULL, 0, FALSE, NULL };

static ESource *
create_source (ESourceRegistry *registry)
{
	ESource *scratch_source;
	ESource *created_source;
	const gchar *uid;
	GError *local_error = NULL;

	/* Configure a minimal scratch source. */
	scratch_source = e_source_new (NULL, NULL, &local_error);
	g_assert_no_error (local_error);
	e_source_set_parent (scratch_source, "local-stub");
	e_source_set_display_name (scratch_source, "Test Commit Source");

	/* Note the scratch source UID. */
	uid = e_source_get_uid (scratch_source);

	/* Commit the scratch source. */
	e_source_registry_commit_source_sync (
		registry, scratch_source, NULL, &local_error);
	g_assert_no_error (local_error);

	/* Obtain the newly-created source from the registry. */
	created_source = e_source_registry_ref_source (registry, uid);

	/* Discard the scratch source. */
	g_clear_object (&scratch_source);

	return created_source;
}

static gboolean
remove_source (ESourceRegistry *registry,
               ESource *source)
{
	ESource *removed_source;
	const gchar *uid;
	gboolean success;
	GError *local_error = NULL;

	if (source == NULL)
		return FALSE;

	/* Request the source be removed. */
	e_source_remove_sync (source, NULL, &local_error);
	g_assert_no_error (local_error);

	/* Verify the registry no longer has the source. */
	uid = e_source_get_uid (source);
	removed_source = e_source_registry_ref_source (registry, uid);
	success = (removed_source == NULL);
	g_clear_object (&removed_source);

	return success;
}

static gboolean
test_create_source_idle_cb (gpointer user_data)
{
	ETestServerFixture *fixture = user_data;
	ESource *source;

	source = create_source (fixture->registry);

	if (source == NULL)
		g_test_fail ();

	g_clear_object (&source);

	g_main_loop_quit (fixture->loop);

	return G_SOURCE_REMOVE;
}

static gboolean
test_remove_source_idle_cb (gpointer user_data)
{
	ETestServerFixture *fixture = user_data;
	ESource *source;

	source = create_source (fixture->registry);

	if (!remove_source (fixture->registry, source))
		g_test_fail ();

	g_clear_object (&source);

	g_main_loop_quit (fixture->loop);

	return G_SOURCE_REMOVE;
}

static void
test_create_source (ETestServerFixture *fixture,
                    gconstpointer user_data)
{
	g_test_bug ("685986");
	g_idle_add (test_create_source_idle_cb, fixture);
	g_main_loop_run (fixture->loop);
}

static void
test_remove_source (ETestServerFixture *fixture,
                    gconstpointer user_data)
{
	g_test_bug ("710668");
	g_idle_add (test_remove_source_idle_cb, fixture);
	g_main_loop_run (fixture->loop);
}

gint
main (gint argc,
      gchar **argv)
{
	gint retval;

	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add (
		"/e-source-registry-test/CreateSource",
		ETestServerFixture, &test_closure,
		e_test_server_utils_setup,
		test_create_source,
		e_test_server_utils_teardown);

	g_test_add (
		"/e-source-registry-test/RemoveSource",
		ETestServerFixture, &test_closure,
		e_test_server_utils_setup,
		test_remove_source,
		e_test_server_utils_teardown);

	retval = e_test_server_utils_run ();

	/* XXX Something is leaking a GDBusConnection reference.
	 *     Leave this disabled until I can track it down. */
	/* g_object_unref (closure.test_dbus); */

	return retval;
}
