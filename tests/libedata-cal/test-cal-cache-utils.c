/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2017 Red Hat, Inc. (www.redhat.com)
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "evolution-data-server-config.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#ifndef G_OS_WIN32
#include <sys/wait.h>
#endif

#include "test-cal-cache-utils.h"

static void
delete_work_directory (const gchar *filename)
{
	/* XXX Instead of complex error checking here, we should ideally use
	 * a recursive GDir / g_unlink() function.
	 *
	 * We cannot use GFile and the recursive delete function without
	 * corrupting our contained D-Bus environment with service files
	 * from the OS.
	 */
	const gchar *argv[] = { "/bin/rm", "-rf", filename, NULL };
	gboolean spawn_succeeded;
	gint exit_status;

	spawn_succeeded = g_spawn_sync (
		NULL, (gchar **) argv, NULL, 0, NULL, NULL,
					NULL, NULL, &exit_status, NULL);

	g_assert (spawn_succeeded);
	#ifndef G_OS_WIN32
	g_assert (WIFEXITED (exit_status));
	g_assert_cmpint (WEXITSTATUS (exit_status), ==, 0);
	#else
	g_assert_cmpint (exit_status, ==, 0);
	#endif
}

void
tcu_fixture_setup (TCUFixture *fixture,
		   gconstpointer user_data)
{
	const TCUClosure *closure = user_data;
	gchar *filename, *directory;
	const gchar *provider_dir;
	GError *error = NULL;

	provider_dir = g_getenv (EDS_CAMEL_PROVIDER_DIR);
	if (!provider_dir)
		provider_dir = CAMEL_PROVIDERDIR;

	if (!g_file_test (provider_dir, G_FILE_TEST_IS_DIR | G_FILE_TEST_EXISTS)) {
		if (g_mkdir_with_parents (provider_dir, 0700) == -1)
			g_warning ("%s: Failed to create folder '%s': %s\n", G_STRFUNC, provider_dir, g_strerror (errno));
	}

	/* Cleanup from last test */
	directory = g_build_filename (g_get_tmp_dir (), "test-cal-cache", NULL);
	delete_work_directory (directory);
	g_free (directory);
	filename = g_build_filename (g_get_tmp_dir (), "test-cal-cache", "cache.db", NULL);

	fixture->cal_cache = e_cal_cache_new (filename, NULL, &error);

	if (!fixture->cal_cache)
		g_error ("Failed to create the ECalCache: %s", error->message);

	g_free (filename);

	if (closure) {
		if (closure->load_set == TCU_LOAD_COMPONENT_SET_EVENTS) {
			tcu_add_component_from_test_case (fixture, "event-1", NULL);
			tcu_add_component_from_test_case (fixture, "event-2", NULL);
			tcu_add_component_from_test_case (fixture, "event-3", NULL);
			tcu_add_component_from_test_case (fixture, "event-4", NULL);
			tcu_add_component_from_test_case (fixture, "event-5", NULL);
			tcu_add_component_from_test_case (fixture, "event-6", NULL);
			tcu_add_component_from_test_case (fixture, "event-6-a", NULL);
			tcu_add_component_from_test_case (fixture, "event-7", NULL);
			tcu_add_component_from_test_case (fixture, "event-8", NULL);
			tcu_add_component_from_test_case (fixture, "event-9", NULL);
		} else if (closure->load_set == TCU_LOAD_COMPONENT_SET_TASKS) {
			tcu_add_component_from_test_case (fixture, "task-1", NULL);
			tcu_add_component_from_test_case (fixture, "task-2", NULL);
			tcu_add_component_from_test_case (fixture, "task-3", NULL);
			tcu_add_component_from_test_case (fixture, "task-4", NULL);
			tcu_add_component_from_test_case (fixture, "task-5", NULL);
			tcu_add_component_from_test_case (fixture, "task-6", NULL);
			tcu_add_component_from_test_case (fixture, "task-7", NULL);
			tcu_add_component_from_test_case (fixture, "task-8", NULL);
			tcu_add_component_from_test_case (fixture, "task-9", NULL);
		}
	}
}

void
tcu_fixture_teardown (TCUFixture *fixture,
		      gconstpointer user_data)
{
	g_object_unref (fixture->cal_cache);
}

gchar *
tcu_get_test_case_filename (const gchar *case_name)
{
	gchar *filename;
	gchar *case_filename;

	case_filename = g_strdup_printf ("%s.ics", case_name);

	/* In the case of installed tests, they run in ${pkglibexecdir}/installed-tests
	 * and the components are installed in ${pkglibexecdir}/installed-tests/components
	 */
	if (g_getenv ("TEST_INSTALLED_SERVICES") != NULL)
		filename = g_build_filename (INSTALLED_TEST_DIR, "components", case_filename, NULL);
	else
		filename = g_build_filename (SRCDIR, "..", "libedata-cal", "components", case_filename, NULL);

	g_free (case_filename);

	return filename;
}

gchar *
tcu_new_icalstring_from_test_case (const gchar *case_name)
{
	gchar *filename;
	GFile * file;
	GError *error = NULL;
	gchar *icalstring = NULL, *uripart;

	filename = tcu_get_test_case_filename (case_name);

	file = g_file_new_for_path (filename);
	if (!g_file_load_contents (file, NULL, &icalstring, NULL, NULL, &error))
		g_error (
			"Failed to read test iCal string file '%s': %s",
			filename, error->message);

	g_free (filename);
	g_object_unref (file);

	uripart = strstr (icalstring, "$EVENT1URI$");
	if (uripart) {
		gchar *uri;
		GString *str;

		filename = tcu_get_test_case_filename ("event-1");
		uri = g_filename_to_uri (filename, NULL, NULL);
		g_free (filename);

		str = g_string_new_len (icalstring, uripart - icalstring);
		g_string_append (str, uri);
		g_string_append (str, uripart + 11);
		g_free (icalstring);
		g_free (uri);

		icalstring = g_string_free (str, FALSE);
	}

	return icalstring;
}

ECalComponent *
tcu_new_component_from_test_case (const gchar *case_name)
{
	gchar *icalstring;
	ECalComponent *component = NULL;

	icalstring = tcu_new_icalstring_from_test_case (case_name);
	if (icalstring)
		component = e_cal_component_new_from_string (icalstring);
	g_free (icalstring);

	if (!component)
		g_error (
			"Failed to construct component from test case '%s'",
			case_name);

	return component;
}

void
tcu_add_component_from_test_case (TCUFixture *fixture,
				  const gchar *case_name,
				  ECalComponent **out_component)
{
	ECalComponent *component;
	GError *error = NULL;

	component = tcu_new_component_from_test_case (case_name);

	if (!e_cal_cache_put_component (fixture->cal_cache, component, case_name, E_CACHE_IS_ONLINE, NULL, &error))
		g_error ("Failed to add component: %s", error->message);

	if (out_component)
		*out_component = g_object_ref (component);

	g_clear_object (&component);
}
