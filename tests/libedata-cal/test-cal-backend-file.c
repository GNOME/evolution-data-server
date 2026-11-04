/*
 * SPDX-FileCopyrightText: (C) 2026 Tobias Mueller <muelli@cryptobitch.de>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "evolution-data-server-config.h"

#include <glib/gstdio.h>

#include <libedata-cal/libedata-cal.h>

#include "e-test-server-utils.h"

static ESourceRegistry *glob_registry = NULL;

static GType
test_get_file_backend_type (void)
{
	ECalBackendFactoryClass *factory_class;
	gchar *filename;
	GType type;

	filename = g_build_filename (g_getenv ("EDS_CALENDAR_MODULES"), "libecalbackendfile.so", NULL);
	g_assert_nonnull (e_module_load_file (filename));
	g_free (filename);

	factory_class = g_type_class_ref (g_type_from_name ("ECalBackendFileEventsFactory"));
	g_assert_nonnull (factory_class);
	type = factory_class->backend_type;
	g_type_class_unref (factory_class);

	return type;
}

static void
test_dispose_dirty (void)
{
	ECalBackendSync *backend;
	ESource *source;
	GFile *file;
	gchar *dirname, *filename;
	GError *error = NULL;

	if (!g_getenv ("EDS_CALENDAR_MODULES")) {
		g_test_skip ("Needs the in-tree file backend module");
		return;
	}

	dirname = g_dir_make_tmp ("test-cal-backend-file-XXXXXX", &error);
	g_assert_no_error (error);
	filename = g_build_filename (dirname, "calendar.ics", NULL);
	file = g_file_new_for_path (filename);

	source = e_source_new_with_uid ("test-cal-backend-file", NULL, &error);
	g_assert_no_error (error);
	e_source_local_set_custom_file (e_source_get_extension (source, E_SOURCE_EXTENSION_LOCAL_BACKEND), file);

	backend = g_object_new (test_get_file_backend_type (),
		"kind", (gulong) I_CAL_VEVENT_COMPONENT,
		"registry", glob_registry,
		"source", source,
		NULL);
	g_assert_nonnull (backend);

	/* opening a missing file creates it and schedules a save in an idle callback */
	e_cal_backend_sync_open (backend, NULL, NULL, &error);
	g_assert_no_error (error);

	g_object_unref (backend);

	/* only compares the pointer, the backend is freed */
	g_assert_null (g_main_context_find_source_by_user_data (NULL, backend));

	g_unlink (filename);
	g_rmdir (dirname);
	g_object_unref (source);
	g_object_unref (file);
	g_free (filename);
	g_free (dirname);
}

gint
main (gint argc,
      gchar **argv)
{
	ETestServerClosure tsclosure = {
		E_TEST_SERVER_NONE,
		NULL, /* Source customization function */
		0,    /* Calendar Type */
		FALSE, /* Keep the working sandbox after the test, don't remove it */
		NULL, /* Destroy Notify function */
	};
	ETestServerFixture tsfixture = { 0 };
	gint res;

	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/");

	e_test_server_utils_prepare_run (argc, argv, 0);
	e_test_server_utils_setup (&tsfixture, &tsclosure);

	glob_registry = tsfixture.registry;
	g_assert_nonnull (glob_registry);

	g_test_add_func ("/ECalBackendFile/DisposeDirty", test_dispose_dirty);

	res = g_test_run ();

	e_test_server_utils_teardown (&tsfixture, &tsclosure);
	e_test_server_utils_finish_run ();

	return res;
}
