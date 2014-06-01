/*
 * e-source-test.c
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <string.h>

#include <libedataserver/libedataserver.h>

#ifndef G_OS_WIN32
#include <gio/gunixoutputstream.h>
#endif

#define SIMPLE_KEY_FILE \
	"[Data Source]\n" \
	"DisplayName=Simple Test Case\n" \
	"Parent=local\n"

typedef struct _TestESource TestESource;
typedef struct _TestFixture TestFixture;

struct _TestESource {
	GFile *file;
	ESource *source;
	gboolean changed;
};

struct _TestFixture {
	TestESource test;
	TestESource same_file;
	TestESource same_content;
	gboolean changed;
};

#if 0  /* ACCOUNT_MGMT */
static void
source_changed_cb (ESource *source,
                   TestESource *test)
{
	test->changed = TRUE;
}

static void
setup_test_source (TestESource *test,
                   const gchar *content)
{
#ifndef G_OS_WIN32
	GOutputStream *stream;
#endif
	gchar *filename;
	gint fd;
	GError *error = NULL;

	/* Create a new temporary key file. */
	fd = g_file_open_tmp ("test-source-XXXXXX", &filename, &error);
	g_assert_no_error (error);

	/* Write the given content to the temporary key file. */
#ifdef G_OS_WIN32
	close (fd);
	g_file_set_contents (filename, content, strlen (content), &error);
#else
	stream = g_unix_output_stream_new (fd, TRUE);
	g_output_stream_write_all (
		stream, content, strlen (content), NULL, NULL, &error);
	g_object_unref (stream);
	g_assert_no_error (error);
#endif

	/* Create a GFile that points to the temporary key file. */
	test->file = g_file_new_for_path (filename);

	/* Create an ESource from the GFile and load the key file. */
	test->source = e_source_new (test->file, &error);
	g_assert_no_error (error);

	g_signal_connect (
		test->source, "changed",
		G_CALLBACK (source_changed_cb), test);

	g_free (filename);
}

static void
teardown_test_source (TestESource *test)
{
	GError *error = NULL;

	/* Delete the temporary key file. */
	g_file_delete (test->file, NULL, &error);
	g_assert_no_error (error);

	g_object_unref (test->file);
	g_object_unref (test->source);
}
#endif /* ACCOUNT_MGMT */

static void
test_fixture_setup_key_file (TestFixture *fixture,
                             gconstpointer test_data)
{
#if 0  /* ACCOUNT_MGMT */
	GError *error = NULL;

	/* The primary ESource for testing. */
	setup_test_source (&fixture->test, SIMPLE_KEY_FILE);

	/* Secondary ESource: different file with identical content. */
	setup_test_source (&fixture->same_content, SIMPLE_KEY_FILE);

	/* Secondary ESource: a clone of the primary ESource. */
	fixture->same_file.file =
		g_file_dup (fixture->test.file);
	fixture->same_file.source =
		e_source_new (fixture->same_file.file, &error);
	g_assert_no_error (error);
#endif /* ACCOUNT_MGMT */
}

static void
test_fixture_teardown_key_file (TestFixture *fixture,
                                gconstpointer test_data)
{
#if 0  /* ACCOUNT_MGMT */
	teardown_test_source (&fixture->test);
#endif
}

static void
test_single_source (TestFixture *fixture,
                    gconstpointer test_data)
{
#if 0  /* ACCOUNT_MGMT */
	GFile *file;
	ESourceExtension *extension;
	const gchar *extension_name;
	const gchar *backend_name;
	const gchar *display_name;
	const gchar *parent;
	const gchar *uid;
	gchar *basename;
	guint hash;
	GError *error = NULL;

	/* Extract a bunch of data from the primary ESource. */
	backend_name = e_source_get_backend_name (fixture->test.source);
	display_name = e_source_get_display_name (fixture->test.source);
	parent = e_source_get_parent (fixture->test.source);
	file = e_source_get_file (fixture->test.source);
	uid = e_source_get_uid (fixture->test.source);
	hash = e_source_hash (fixture->test.source);

	/* An ESource's GFile should compare equal to the GFile
	 * used to create it. */
	g_assert (g_file_equal (file, fixture->test.file));

	/* An ESource's UID should match the key file's basename. */
	basename = g_file_get_basename (fixture->test.file);
	g_assert_cmpstr (uid, ==, basename);
	g_free (basename);

	/* An ESource's hash value is based on its UID. */
	g_assert_cmpuint (hash, ==, g_str_hash (uid));

	/* Check the other fields specified in this key file. */
	g_assert_cmpstr (display_name, ==, "Simple Test Case");
	g_assert_cmpstr (parent, ==, "local");

	/* The backend name was omitted, so it should be NULL. */
	g_assert_cmpstr (backend_name, ==, NULL);

	/* ESource equality is based solely on UIDs. */
	g_assert (e_source_equal (
		fixture->test.source, fixture->test.source));
	g_assert (e_source_equal (
		fixture->test.source, fixture->same_file.source));
	g_assert (!e_source_equal (
		fixture->test.source, fixture->same_content.source));

	/* Nothing done so far should have triggered ESource::changed. */
	g_assert (!fixture->test.changed);

	/* Check on-demand extension loading. */
	E_TYPE_SOURCE_AUTHENTICATION;  /* register the type */
	extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
	g_assert (!e_source_has_extension (
		fixture->test.source, extension_name));
	extension = e_source_get_extension (
		fixture->test.source, extension_name);
	g_assert (e_source_has_extension (
		fixture->test.source, extension_name));
	g_assert (E_IS_SOURCE_AUTHENTICATION (extension));

	/* Loading an extension should trigger ESource::changed. */
	g_assert (fixture->test.changed);

	fixture->test.changed = FALSE;

	/* The ESource::changed signal is wired to its own "notify" and
	 * the "notify" signals of its extensions.  Passing any property
	 * to g_object_notify() should trigger it. */
	g_object_notify (G_OBJECT (fixture->test.source), "display-name");
	g_assert (fixture->test.changed);

	fixture->test.changed = FALSE;

	/* Now try the extension we loaded. */
	g_object_notify (G_OBJECT (extension), "host");
	g_assert (fixture->test.changed);

	/* The primary ESource and cloned ESource both handle the same
	 * key file.  If we change a property in one ESource and sync it
	 * to disk, then reload the other ESource and examine the same
	 * property, the values should match. */
	display_name = "Modified Test Case";
	e_source_set_display_name (fixture->test.source, display_name);
	e_source_sync (fixture->test.source, &error);
	g_assert_no_error (error);
	e_source_reload (fixture->same_file.source, &error);
	g_assert_no_error (error);
	display_name = e_source_get_display_name (fixture->same_file.source);
	g_assert_cmpstr (display_name, ==, "Modified Test Case");
#endif /* ACOCUNT_MGMT */
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add (
		"/e-source-test/SingleSource",
		TestFixture, NULL,
		test_fixture_setup_key_file,
		test_single_source,
		test_fixture_teardown_key_file);

	return g_test_run ();
}
