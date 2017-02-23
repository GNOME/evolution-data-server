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

#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <libecal/libecal.h>

#include "test-cal-cache-utils.h"

#define EXPECTED_TZID		"/freeassociation.sourceforge.net/America/New_York"
#define EXPECTED_LOCATION	"America/New_York"
#define REMOTE_URL		"https://www.gnome.org/wp-content/themes/gnome-grass/images/gnome-logo.svg"

typedef struct _ECalMetaBackendTest {
	ECalMetaBackend parent;
} ECalMetaBackendTest;

typedef struct _ECalMetaBackendTestClass {
	ECalMetaBackendClass parent_class;
} ECalMetaBackendTestClass;

#define E_TYPE_CAL_META_BACKEND_TEST (e_cal_meta_backend_test_get_type ())

GType e_cal_meta_backend_test_get_type (void) G_GNUC_CONST;

G_DEFINE_TYPE (ECalMetaBackendTest, e_cal_meta_backend_test, E_TYPE_CAL_META_BACKEND)

static void
e_cal_meta_backend_test_class_init (ECalMetaBackendTestClass *klass)
{
}

static void
e_cal_meta_backend_test_init (ECalMetaBackendTest *test)
{
}

static ECalMetaBackend *
e_cal_meta_backend_test_new (ECalCache *cache)
{
	ECalMetaBackend *meta_backend;
	ESourceRegistry *registry;
	ESource *scratch;
	GError *error = NULL;

	g_assert (E_IS_CAL_CACHE (cache));

	registry = e_source_registry_new_sync (NULL, NULL);
	g_assert_nonnull (registry);

	scratch = e_source_new_with_uid ("test-source", NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (scratch);

	meta_backend = g_object_new (E_TYPE_CAL_META_BACKEND_TEST,
		"source", scratch,
		"registry", registry,
		"kind", ICAL_VEVENT_COMPONENT,
		NULL);
	g_assert_nonnull (meta_backend);

	g_object_unref (registry);
	g_object_unref (scratch);

	e_cal_meta_backend_set_cache (meta_backend, cache);

	return meta_backend;
}

static void
assert_tzid_matches_cb (icalparameter *param,
			gpointer user_data)
{
	const gchar *expected_tzid = user_data;

	g_assert_cmpstr (icalparameter_get_tzid (param), ==, expected_tzid);
}

static void
test_merge_instances (TCUFixture *fixture,
		      gconstpointer user_data)
{
	ECalMetaBackend *meta_backend;
	GSList *instances = NULL;
	icalcomponent *icalcomp, *subcomp;
	icalproperty *prop;
	gboolean success;
	GError *error = NULL;

	meta_backend = e_cal_meta_backend_test_new (fixture->cal_cache);
	g_assert_nonnull (meta_backend);

	/* event-1 has only UTC times, with no TZID */
	success = e_cal_cache_get_components_by_uid (fixture->cal_cache, "event-1", &instances, NULL, &error);
	g_assert_no_error (error);
	g_assert (success);
	g_assert_nonnull (instances);

	/* TZID as is */
	icalcomp = e_cal_meta_backend_merge_instances (meta_backend, instances, FALSE);
	g_assert_nonnull (icalcomp);
	g_assert_cmpint (icalcomponent_isa (icalcomp), ==, ICAL_VCALENDAR_COMPONENT);
	g_assert_cmpint (icalcomponent_count_components (icalcomp, ICAL_ANY_COMPONENT), ==, 1);

	subcomp = icalcomponent_get_first_component (icalcomp, ICAL_ANY_COMPONENT);
	g_assert_nonnull (subcomp);
	g_assert_cmpint (icalcomponent_isa (subcomp), ==, ICAL_VEVENT_COMPONENT);

	icalcomponent_free (icalcomp);

	/* TZID as location */
	icalcomp = e_cal_meta_backend_merge_instances (meta_backend, instances, TRUE);
	g_assert_nonnull (icalcomp);
	g_assert_cmpint (icalcomponent_isa (icalcomp), ==, ICAL_VCALENDAR_COMPONENT);
	g_assert_cmpint (icalcomponent_count_components (icalcomp, ICAL_ANY_COMPONENT), ==, 1);

	subcomp = icalcomponent_get_first_component (icalcomp, ICAL_ANY_COMPONENT);
	g_assert_nonnull (subcomp);
	g_assert_cmpint (icalcomponent_isa (subcomp), ==, ICAL_VEVENT_COMPONENT);

	icalcomponent_free (icalcomp);

	g_slist_free_full (instances, g_object_unref);
	instances = NULL;

	/* event-7 has built-in TZID */
	success = e_cal_cache_get_components_by_uid (fixture->cal_cache, "event-7", &instances, NULL, &error);
	g_assert_no_error (error);
	g_assert (success);
	g_assert_nonnull (instances);

	/* TZID as is */
	icalcomp = e_cal_meta_backend_merge_instances (meta_backend, instances, FALSE);
	g_assert_nonnull (icalcomp);
	g_assert_cmpint (icalcomponent_isa (icalcomp), ==, ICAL_VCALENDAR_COMPONENT);
	g_assert_cmpint (icalcomponent_count_components (icalcomp, ICAL_ANY_COMPONENT), ==, 2);
	g_assert_cmpint (icalcomponent_count_components (icalcomp, ICAL_VTIMEZONE_COMPONENT), ==, 1);
	g_assert_cmpint (icalcomponent_count_components (icalcomp, ICAL_VEVENT_COMPONENT), ==, 1);

	subcomp = icalcomponent_get_first_component (icalcomp, ICAL_VTIMEZONE_COMPONENT);
	g_assert_nonnull (subcomp);
	g_assert_cmpint (icalcomponent_isa (subcomp), ==, ICAL_VTIMEZONE_COMPONENT);

	prop = icalcomponent_get_first_property (subcomp, ICAL_TZID_PROPERTY);
	g_assert_nonnull (prop);
	g_assert_cmpstr (icalproperty_get_tzid (prop), ==, EXPECTED_TZID);

	subcomp = icalcomponent_get_first_component (icalcomp, ICAL_VEVENT_COMPONENT);
	g_assert_nonnull (subcomp);
	icalcomponent_foreach_tzid (subcomp, assert_tzid_matches_cb, (gpointer) icalproperty_get_tzid (prop));

	icalcomponent_free (icalcomp);

	/* TZID to location */
	icalcomp = e_cal_meta_backend_merge_instances (meta_backend, instances, TRUE);
	g_assert_nonnull (icalcomp);
	g_assert_cmpint (icalcomponent_isa (icalcomp), ==, ICAL_VCALENDAR_COMPONENT);
	g_assert_cmpint (icalcomponent_count_components (icalcomp, ICAL_ANY_COMPONENT), ==, 2);
	g_assert_cmpint (icalcomponent_count_components (icalcomp, ICAL_VTIMEZONE_COMPONENT), ==, 1);
	g_assert_cmpint (icalcomponent_count_components (icalcomp, ICAL_VEVENT_COMPONENT), ==, 1);

	subcomp = icalcomponent_get_first_component (icalcomp, ICAL_VTIMEZONE_COMPONENT);
	g_assert_nonnull (subcomp);
	g_assert_cmpint (icalcomponent_isa (subcomp), ==, ICAL_VTIMEZONE_COMPONENT);

	prop = icalcomponent_get_first_property (subcomp, ICAL_TZID_PROPERTY);
	g_assert_nonnull (prop);
	g_assert_cmpstr (icalproperty_get_tzid (prop), ==, EXPECTED_LOCATION);

	subcomp = icalcomponent_get_first_component (icalcomp, ICAL_VEVENT_COMPONENT);
	g_assert_nonnull (subcomp);
	icalcomponent_foreach_tzid (subcomp, assert_tzid_matches_cb, (gpointer) icalproperty_get_tzid (prop));

	icalcomponent_free (icalcomp);
	g_slist_free_full (instances, g_object_unref);
	instances = NULL;

	/* event-6 has TZID-s as locations already and a detached instance */
	success = e_cal_cache_get_components_by_uid (fixture->cal_cache, "event-6", &instances, NULL, &error);
	g_assert_no_error (error);
	g_assert (success);
	g_assert_nonnull (instances);

	/* TZID as is */
	icalcomp = e_cal_meta_backend_merge_instances (meta_backend, instances, FALSE);
	g_assert_nonnull (icalcomp);
	g_assert_cmpint (icalcomponent_isa (icalcomp), ==, ICAL_VCALENDAR_COMPONENT);
	g_assert_cmpint (icalcomponent_count_components (icalcomp, ICAL_ANY_COMPONENT), ==, 3);
	g_assert_cmpint (icalcomponent_count_components (icalcomp, ICAL_VTIMEZONE_COMPONENT), ==, 1);
	g_assert_cmpint (icalcomponent_count_components (icalcomp, ICAL_VEVENT_COMPONENT), ==, 2);

	subcomp = icalcomponent_get_first_component (icalcomp, ICAL_VTIMEZONE_COMPONENT);
	g_assert_nonnull (subcomp);
	g_assert_cmpint (icalcomponent_isa (subcomp), ==, ICAL_VTIMEZONE_COMPONENT);

	prop = icalcomponent_get_first_property (subcomp, ICAL_TZID_PROPERTY);
	g_assert_nonnull (prop);
	g_assert_cmpstr (icalproperty_get_tzid (prop), ==, EXPECTED_LOCATION);

	subcomp = icalcomponent_get_first_component (icalcomp, ICAL_VEVENT_COMPONENT);
	g_assert_nonnull (subcomp);
	icalcomponent_foreach_tzid (subcomp, assert_tzid_matches_cb, (gpointer) icalproperty_get_tzid (prop));

	subcomp = icalcomponent_get_next_component (icalcomp, ICAL_VEVENT_COMPONENT);
	g_assert_nonnull (subcomp);
	icalcomponent_foreach_tzid (subcomp, assert_tzid_matches_cb, (gpointer) icalproperty_get_tzid (prop));

	icalcomponent_free (icalcomp);

	/* TZID to location */
	icalcomp = e_cal_meta_backend_merge_instances (meta_backend, instances, TRUE);
	g_assert_nonnull (icalcomp);
	g_assert_cmpint (icalcomponent_isa (icalcomp), ==, ICAL_VCALENDAR_COMPONENT);
	g_assert_cmpint (icalcomponent_count_components (icalcomp, ICAL_ANY_COMPONENT), ==, 3);
	g_assert_cmpint (icalcomponent_count_components (icalcomp, ICAL_VTIMEZONE_COMPONENT), ==, 1);
	g_assert_cmpint (icalcomponent_count_components (icalcomp, ICAL_VEVENT_COMPONENT), ==, 2);

	subcomp = icalcomponent_get_first_component (icalcomp, ICAL_VTIMEZONE_COMPONENT);
	g_assert_nonnull (subcomp);
	g_assert_cmpint (icalcomponent_isa (subcomp), ==, ICAL_VTIMEZONE_COMPONENT);

	prop = icalcomponent_get_first_property (subcomp, ICAL_TZID_PROPERTY);
	g_assert_nonnull (prop);
	g_assert_cmpstr (icalproperty_get_tzid (prop), ==, EXPECTED_LOCATION);

	subcomp = icalcomponent_get_first_component (icalcomp, ICAL_VEVENT_COMPONENT);
	g_assert_nonnull (subcomp);
	icalcomponent_foreach_tzid (subcomp, assert_tzid_matches_cb, (gpointer) icalproperty_get_tzid (prop));

	subcomp = icalcomponent_get_next_component (icalcomp, ICAL_VEVENT_COMPONENT);
	g_assert_nonnull (subcomp);
	icalcomponent_foreach_tzid (subcomp, assert_tzid_matches_cb, (gpointer) icalproperty_get_tzid (prop));

	icalcomponent_free (icalcomp);
	g_slist_free_full (instances, g_object_unref);

	g_object_unref (meta_backend);
}

static void
check_attachment_content (icalattach *attach,
			  const gchar *expected_content,
			  gsize expected_content_len)
{
	g_assert_nonnull (attach);
	g_assert_nonnull (expected_content);
	g_assert_cmpint (expected_content_len, >, 0);

	if (icalattach_get_is_url (attach)) {
		const gchar *url;
		gboolean success;
		gchar *filename;
		gchar *content = NULL;
		gsize content_len = -1;
		GError *error = NULL;

		url = icalattach_get_url (attach);
		g_assert_nonnull (url);
		g_assert (g_str_has_prefix (url, "file://"));

		filename = g_filename_from_uri (icalattach_get_url (attach), NULL, &error);
		g_assert_no_error (error);
		g_assert_nonnull (filename);

		success = g_file_get_contents (filename, &content, &content_len, &error);
		g_assert_no_error (error);
		g_assert (success);
		g_assert_nonnull (content);
		g_assert_cmpint (content_len, >, 0);

		g_assert_cmpmem (content, content_len, expected_content, expected_content_len);

		g_free (filename);
		g_free (content);
	} else {
		guchar *base64;
		gsize base64_len;

		base64 = g_base64_decode ((const gchar *) icalattach_get_data (attach), &base64_len);
		g_assert_nonnull (base64);
		g_assert_cmpmem (base64, base64_len, expected_content, expected_content_len);

		g_free (base64);
	}
}

static void
test_attachments (TCUFixture *fixture,
		  gconstpointer user_data)
{
	ECalMetaBackend *meta_backend;
	gchar *content = NULL;
	gsize content_len = 0;
	ECalComponent *comp = NULL;
	icalcomponent *icalcomp;
	icalproperty *prop;
	icalparameter *param;
	icalattach *attach;
	gchar *filename;
	const gchar *basename;
	gboolean success;
	GError *error = NULL;

	meta_backend = e_cal_meta_backend_test_new (fixture->cal_cache);
	g_assert_nonnull (meta_backend);

	/* It has a URL attachment */
	success = e_cal_cache_get_component (fixture->cal_cache, "event-7", NULL, &comp, NULL, &error);
	g_assert_no_error (error);
	g_assert (success);
	g_assert_nonnull (comp);

	icalcomp = icalcomponent_new_clone (e_cal_component_get_icalcomponent (comp));
	g_assert_nonnull (icalcomp);
	g_assert_cmpint (icalcomponent_count_properties (icalcomp, ICAL_ATTACH_PROPERTY), ==, 1);

	prop = icalcomponent_get_first_property (icalcomp, ICAL_ATTACH_PROPERTY);
	g_assert_nonnull (prop);
	g_assert_null (icalproperty_get_first_parameter (prop, ICAL_FILENAME_PARAMETER));

	attach = icalproperty_get_attach (prop);
	g_assert_nonnull (attach);
	g_assert (icalattach_get_is_url (attach));

	filename = g_filename_from_uri (icalattach_get_url (attach), NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (filename);

	basename = strrchr (filename, '/');
	g_assert_nonnull (basename);
	basename++;

	success = g_file_get_contents (filename, &content, &content_len, &error);
	g_assert_no_error (error);
	g_assert (success);
	g_assert_nonnull (content);
	g_assert_cmpint (content_len, >, 0);

	success = e_cal_meta_backend_inline_local_attachments_sync (meta_backend, icalcomp, NULL, &error);
	g_assert_no_error (error);
	g_assert (success);
	g_assert_cmpint (icalcomponent_count_properties (icalcomp, ICAL_ATTACH_PROPERTY), ==, 1);

	prop = icalcomponent_get_first_property (icalcomp, ICAL_ATTACH_PROPERTY);
	g_assert_nonnull (prop);
	g_assert_nonnull (icalproperty_get_first_parameter (prop, ICAL_FILENAME_PARAMETER));
	g_assert_nonnull (icalproperty_get_first_parameter (prop, ICAL_VALUE_PARAMETER));
	g_assert_nonnull (icalproperty_get_first_parameter (prop, ICAL_ENCODING_PARAMETER));

	param = icalproperty_get_first_parameter (prop, ICAL_FILENAME_PARAMETER);
	g_assert_cmpstr (icalparameter_get_filename (param), ==, basename);

	attach = icalproperty_get_attach (prop);
	g_assert_nonnull (attach);
	g_assert (!icalattach_get_is_url (attach));

	check_attachment_content (attach, content, content_len);

	success = e_cal_meta_backend_store_inline_attachments_sync (meta_backend, icalcomp, NULL, &error);
	g_assert_no_error (error);
	g_assert (success);
	g_assert_cmpint (icalcomponent_count_properties (icalcomp, ICAL_ATTACH_PROPERTY), ==, 1);

	prop = icalcomponent_get_first_property (icalcomp, ICAL_ATTACH_PROPERTY);
	g_assert_nonnull (prop);
	g_assert_nonnull (icalproperty_get_first_parameter (prop, ICAL_FILENAME_PARAMETER));
	g_assert_null (icalproperty_get_first_parameter (prop, ICAL_VALUE_PARAMETER));
	g_assert_null (icalproperty_get_first_parameter (prop, ICAL_ENCODING_PARAMETER));

	param = icalproperty_get_first_parameter (prop, ICAL_FILENAME_PARAMETER);
	g_assert_cmpstr (icalparameter_get_filename (param), ==, basename);

	attach = icalproperty_get_attach (prop);
	g_assert_nonnull (attach);
	g_assert (icalattach_get_is_url (attach));

	check_attachment_content (attach, content, content_len);

	/* Add a URL attachment which is not pointing to a local file */
	attach = icalattach_new_from_url (REMOTE_URL);
	prop = icalproperty_new_attach (attach);
	icalattach_unref (attach);
	icalcomponent_add_property (icalcomp, prop);

	g_assert_cmpint (icalcomponent_count_properties (icalcomp, ICAL_ATTACH_PROPERTY), ==, 2);

	success = e_cal_meta_backend_inline_local_attachments_sync (meta_backend, icalcomp, NULL, &error);
	g_assert_no_error (error);
	g_assert (success);
	g_assert_cmpint (icalcomponent_count_properties (icalcomp, ICAL_ATTACH_PROPERTY), ==, 2);

	prop = icalcomponent_get_first_property (icalcomp, ICAL_ATTACH_PROPERTY);
	g_assert_nonnull (prop);
	g_assert_nonnull (icalproperty_get_first_parameter (prop, ICAL_FILENAME_PARAMETER));
	g_assert_nonnull (icalproperty_get_first_parameter (prop, ICAL_VALUE_PARAMETER));
	g_assert_nonnull (icalproperty_get_first_parameter (prop, ICAL_ENCODING_PARAMETER));

	param = icalproperty_get_first_parameter (prop, ICAL_FILENAME_PARAMETER);
	g_assert_cmpstr (icalparameter_get_filename (param), ==, basename);

	attach = icalproperty_get_attach (prop);
	g_assert_nonnull (attach);
	g_assert (!icalattach_get_is_url (attach));

	check_attachment_content (attach, content, content_len);

	/* Verify the remote URL did not change */
	prop = icalcomponent_get_next_property (icalcomp, ICAL_ATTACH_PROPERTY);
	g_assert_nonnull (prop);
	g_assert_null (icalproperty_get_first_parameter (prop, ICAL_FILENAME_PARAMETER));

	attach = icalproperty_get_attach (prop);
	g_assert_nonnull (attach);
	g_assert (icalattach_get_is_url (attach));
	g_assert_cmpstr (icalattach_get_url (attach), ==, REMOTE_URL);

	success = e_cal_meta_backend_store_inline_attachments_sync (meta_backend, icalcomp, NULL, &error);
	g_assert_no_error (error);
	g_assert (success);
	g_assert_cmpint (icalcomponent_count_properties (icalcomp, ICAL_ATTACH_PROPERTY), ==, 2);

	prop = icalcomponent_get_first_property (icalcomp, ICAL_ATTACH_PROPERTY);
	g_assert_nonnull (prop);
	g_assert_nonnull (icalproperty_get_first_parameter (prop, ICAL_FILENAME_PARAMETER));
	g_assert_null (icalproperty_get_first_parameter (prop, ICAL_VALUE_PARAMETER));
	g_assert_null (icalproperty_get_first_parameter (prop, ICAL_ENCODING_PARAMETER));

	param = icalproperty_get_first_parameter (prop, ICAL_FILENAME_PARAMETER);
	g_assert_cmpstr (icalparameter_get_filename (param), ==, basename);

	attach = icalproperty_get_attach (prop);
	g_assert_nonnull (attach);
	g_assert (icalattach_get_is_url (attach));

	check_attachment_content (attach, content, content_len);

	/* Verify the remote URL did not change */
	prop = icalcomponent_get_next_property (icalcomp, ICAL_ATTACH_PROPERTY);
	g_assert_nonnull (prop);
	g_assert_null (icalproperty_get_first_parameter (prop, ICAL_FILENAME_PARAMETER));

	attach = icalproperty_get_attach (prop);
	g_assert_nonnull (attach);
	g_assert (icalattach_get_is_url (attach));
	g_assert_cmpstr (icalattach_get_url (attach), ==, REMOTE_URL);

	icalcomponent_free (icalcomp);
	g_object_unref (meta_backend);
	g_object_unref (comp);
	g_free (filename);
	g_free (content);
}

gint
main (gint argc,
      gchar **argv)
{
	TCUClosure closure_events = { TCU_LOAD_COMPONENT_SET_EVENTS };

#if !GLIB_CHECK_VERSION (2, 35, 1)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);

	/* Ensure that the client and server get the same locale */
	g_assert (g_setenv ("LC_ALL", "en_US.UTF-8", TRUE));
	setlocale (LC_ALL, "");

#ifdef HAVE_ICALTZUTIL_SET_EXACT_VTIMEZONES_SUPPORT
	icaltzutil_set_exact_vtimezones_support (0);
#endif

	g_test_add ("/ECalMetaBackend/MergeInstances", TCUFixture, &closure_events,
		tcu_fixture_setup, test_merge_instances, tcu_fixture_teardown);
	g_test_add ("/ECalMetaBackend/Attachments", TCUFixture, &closure_events,
		tcu_fixture_setup, test_attachments, tcu_fixture_teardown);

	return g_test_run ();
}
