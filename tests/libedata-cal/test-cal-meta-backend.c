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

#include "libecal/libecal.h"

#include "e-test-server-utils.h"
#include "test-cal-cache-utils.h"

void _e_cal_cache_remove_loaded_timezones (ECalCache *cal_cache); /* e-cal-cache.c, private function */
void _e_cal_backend_remove_cached_timezones (ECalBackend *cal_backend); /* e-cal-backend.c, private function */

#define EXPECTED_TZID		"/freeassociation.sourceforge.net/America/New_York"
#define EXPECTED_LOCATION	"America/New_York"
#define REMOTE_URL		"https://www.gnome.org/wp-content/themes/gnome-grass/images/gnome-logo.svg"
#define MODIFIED_SUMMARY_STR	"Modified summary"

typedef struct _ECalMetaBackendTest {
	ECalMetaBackend parent;

	icalcomponent *vcalendar;

	gint sync_tag_index;
	gboolean can_connect;
	gboolean is_connected;
	gint connect_count;
	gint list_count;
	gint save_count;
	gint load_count;
	gint remove_count;
} ECalMetaBackendTest;

typedef struct _ECalMetaBackendTestClass {
	ECalMetaBackendClass parent_class;
} ECalMetaBackendTestClass;

#define E_TYPE_CAL_META_BACKEND_TEST (e_cal_meta_backend_test_get_type ())
#define E_CAL_META_BACKEND_TEST(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_CAL_META_BACKEND_TEST, ECalMetaBackendTest))
#define E_IS_CAL_META_BACKEND_TEST(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_CAL_META_BACKEND_TEST))

GType e_cal_meta_backend_test_get_type (void) G_GNUC_CONST;

G_DEFINE_TYPE (ECalMetaBackendTest, e_cal_meta_backend_test, E_TYPE_CAL_META_BACKEND)

static void
ecmb_test_add_test_case (ECalMetaBackendTest *test_backend,
			 const gchar *case_name)
{
	gchar *icalstr;
	icalcomponent *icalcomp;

	g_assert_nonnull (test_backend);
	g_assert_nonnull (case_name);

	icalstr = tcu_new_icalstring_from_test_case (case_name);
	g_assert_nonnull (icalstr);

	icalcomp = icalcomponent_new_from_string (icalstr);
	g_assert_nonnull (icalcomp);
	g_free (icalstr);

	icalcomponent_add_component (test_backend->vcalendar, icalcomp);
}

static void
ecmb_test_remove_component (ECalMetaBackendTest *test_backend,
			    const gchar *uid,
			    const gchar *rid)
{
	icalcomponent *icalcomp;

	g_assert_nonnull (test_backend);
	g_assert_nonnull (uid);

	if (rid && !*rid)
		rid = NULL;

	for (icalcomp = icalcomponent_get_first_component (test_backend->vcalendar, ICAL_VEVENT_COMPONENT);
	     icalcomp;) {
		const gchar *server_uid;

		server_uid = icalcomponent_get_uid (icalcomp);
		g_assert_nonnull (server_uid);

		if (g_str_equal (server_uid, uid) && (!rid || !*rid ||
		    (icalcomponent_get_first_property (icalcomp, ICAL_RECURRENCEID_PROPERTY) &&
		    g_str_equal (rid, icaltime_as_ical_string (icalcomponent_get_recurrenceid (icalcomp)))))) {
			icalcomponent_remove_component (test_backend->vcalendar, icalcomp);
			icalcomponent_free (icalcomp);

			icalcomp = icalcomponent_get_first_component (test_backend->vcalendar, ICAL_VEVENT_COMPONENT);
		} else {
			icalcomp = icalcomponent_get_next_component (test_backend->vcalendar, ICAL_VEVENT_COMPONENT);
		}
	}
}

static GHashTable * /* ECalComponentId * ~> NULL */
ecmb_test_gather_ids (va_list args)
{
	GHashTable *expects;
	const gchar *uid, *rid;

	expects = g_hash_table_new_full ((GHashFunc) e_cal_component_id_hash, (GEqualFunc) e_cal_component_id_equal,
		(GDestroyNotify) e_cal_component_free_id, NULL);

	uid = va_arg (args, const gchar *);
	while (uid) {
		rid = va_arg (args, const gchar *);

		g_hash_table_insert (expects, e_cal_component_id_new (uid, rid), NULL);
		uid = va_arg (args, const gchar *);
	}

	return expects;
}

static void
ecmb_test_vcalendar_contains (icalcomponent *vcalendar,
			      gboolean negate,
			      gboolean exact,
			      ...) /* <uid, rid> pairs, ended with NULL */
{
	va_list args;
	GHashTable *expects;
	icalcomponent *icalcomp;
	guint ntotal;

	g_return_if_fail (vcalendar != NULL);
	g_return_if_fail (icalcomponent_isa (vcalendar) == ICAL_VCALENDAR_COMPONENT);

	va_start (args, exact);
	expects = ecmb_test_gather_ids (args);
	va_end (args);

	ntotal = g_hash_table_size (expects);

	for (icalcomp = icalcomponent_get_first_component (vcalendar, ICAL_VEVENT_COMPONENT);
	     icalcomp;
	     icalcomp = icalcomponent_get_next_component (vcalendar, ICAL_VEVENT_COMPONENT)) {
		ECalComponentId id;

		id.uid = (gpointer) icalcomponent_get_uid (icalcomp);
		if (icalcomponent_get_first_property (icalcomp, ICAL_RECURRENCEID_PROPERTY))
			id.rid = (gpointer) icaltime_as_ical_string (icalcomponent_get_recurrenceid (icalcomp));
		else
			id.rid = NULL;

		if (exact) {
			if (negate)
				g_assert (!g_hash_table_remove (expects, &id));
			else
				g_assert (g_hash_table_remove (expects, &id));
		} else {
			g_hash_table_remove (expects, &id);
		}
	}

	if (negate)
		g_assert_cmpint (g_hash_table_size (expects), ==, ntotal);
	else
		g_assert_cmpint (g_hash_table_size (expects), ==, 0);

	g_hash_table_destroy (expects);
}

static void
ecmb_test_cache_contains (ECalCache *cal_cache,
			  gboolean negate,
			  gboolean exact,
			  ...) /* <uid, rid> pairs, ended with NULL */
{
	va_list args;
	GHashTable *expects;
	GHashTableIter iter;
	gpointer key;
	gint found = 0;

	g_return_if_fail (E_IS_CAL_CACHE (cal_cache));

	va_start (args, exact);
	expects = ecmb_test_gather_ids (args);
	va_end (args);

	g_hash_table_iter_init (&iter, expects);
	while (g_hash_table_iter_next (&iter, &key, NULL)) {
		ECalComponentId *id = key;

		g_assert_nonnull (id);

		if (e_cal_cache_contains (cal_cache, id->uid, id->rid, E_CACHE_EXCLUDE_DELETED))
			found++;
	}

	if (negate)
		g_assert_cmpint (0, ==, found);
	else
		g_assert_cmpint (g_hash_table_size (expects), ==, found);

	g_hash_table_destroy (expects);

	if (exact && !negate)
		g_assert_cmpint (e_cache_get_count (E_CACHE (cal_cache), E_CACHE_EXCLUDE_DELETED, NULL, NULL), ==, found);
}

static void
ecmb_test_cache_and_server_equal (ECalCache *cal_cache,
				  icalcomponent *vcalendar,
				  ECacheDeletedFlag deleted_flag)
{
	icalcomponent *icalcomp;

	g_return_if_fail (E_IS_CAL_CACHE (cal_cache));
	g_return_if_fail (vcalendar != NULL);

	g_assert_cmpint (e_cache_get_count (E_CACHE (cal_cache), deleted_flag, NULL, NULL), ==,
		icalcomponent_count_components (vcalendar, ICAL_VEVENT_COMPONENT));

	for (icalcomp = icalcomponent_get_first_component (vcalendar, ICAL_VEVENT_COMPONENT);
	     icalcomp;
	     icalcomp = icalcomponent_get_next_component (vcalendar, ICAL_VEVENT_COMPONENT)) {
		const gchar *uid, *rid = NULL;

		uid = icalcomponent_get_uid (icalcomp);
		if (icalcomponent_get_first_property (icalcomp, ICAL_RECURRENCEID_PROPERTY))
			rid = icaltime_as_ical_string (icalcomponent_get_recurrenceid (icalcomp));

		g_assert (e_cal_cache_contains (cal_cache, uid, rid, deleted_flag));
	}
}

static gchar *
e_cal_meta_backend_test_get_backend_property (ECalBackend *cal_backend,
					      const gchar *prop_name)
{
	g_return_val_if_fail (E_IS_CAL_META_BACKEND_TEST (cal_backend), NULL);
	g_return_val_if_fail (prop_name != NULL, NULL);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		return g_strjoin (",",
			e_cal_meta_backend_get_capabilities (E_CAL_META_BACKEND (cal_backend)),
			CAL_STATIC_CAPABILITY_ALARM_DESCRIPTION,
			NULL);
	} else if (g_str_equal (prop_name, CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS)) {
		return g_strdup ("user@no.where");
	}

	/* Chain up to parent's method. */
	return E_CAL_BACKEND_CLASS (e_cal_meta_backend_test_parent_class)->get_backend_property (cal_backend, prop_name);
}

static gboolean
e_cal_meta_backend_test_connect_sync (ECalMetaBackend *meta_backend,
				      const ENamedParameters *credentials,
				      ESourceAuthenticationResult *out_auth_result,
				      gchar **out_certificate_pem,
				      GTlsCertificateFlags *out_certificate_errors,
				      GCancellable *cancellable,
				      GError **error)
{
	ECalMetaBackendTest *test_backend;

	g_return_val_if_fail (E_IS_CAL_META_BACKEND_TEST (meta_backend), FALSE);

	test_backend = E_CAL_META_BACKEND_TEST (meta_backend);

	if (test_backend->is_connected)
		return TRUE;

	test_backend->connect_count++;

	if (test_backend->can_connect) {
		test_backend->is_connected = TRUE;
		return TRUE;
	}

	g_set_error_literal (error, E_CLIENT_ERROR, E_CLIENT_ERROR_REPOSITORY_OFFLINE,
		e_client_error_to_string (E_CLIENT_ERROR_REPOSITORY_OFFLINE));

	return FALSE;
}

static gboolean
e_cal_meta_backend_test_disconnect_sync (ECalMetaBackend *meta_backend,
					 GCancellable *cancellable,
					 GError **error)
{
	ECalMetaBackendTest *test_backend;

	g_return_val_if_fail (E_IS_CAL_META_BACKEND_TEST (meta_backend), FALSE);

	test_backend = E_CAL_META_BACKEND_TEST (meta_backend);
	test_backend->is_connected = FALSE;

	return TRUE;
}

static gboolean
e_cal_meta_backend_test_get_changes_sync (ECalMetaBackend *meta_backend,
					  const gchar *last_sync_tag,
					  gboolean is_repeat,
					  gchar **out_new_sync_tag,
					  gboolean *out_repeat,
					  GSList **out_created_objects,
					  GSList **out_modified_objects,
					  GSList **out_removed_objects,
					  GCancellable *cancellable,
					  GError **error)
{
	ECalMetaBackendTest *test_backend;

	g_return_val_if_fail (E_IS_CAL_META_BACKEND_TEST (meta_backend), FALSE);
	g_return_val_if_fail (out_new_sync_tag != NULL, FALSE);
	g_return_val_if_fail (out_repeat != NULL, FALSE);

	test_backend = E_CAL_META_BACKEND_TEST (meta_backend);

	if (!test_backend->sync_tag_index) {
		g_assert_null (last_sync_tag);
	} else {
		g_assert_nonnull (last_sync_tag);
		g_assert_cmpint (atoi (last_sync_tag), ==, test_backend->sync_tag_index);
	}

	test_backend->sync_tag_index++;
	*out_new_sync_tag = g_strdup_printf ("%d", test_backend->sync_tag_index);

	if (test_backend->sync_tag_index == 2)
		*out_repeat = TRUE;
	else if (test_backend->sync_tag_index == 3)
		return TRUE;

	/* Nothing to do here at the moment, left the work to the parent class,
	   which calls list_existing_sync() internally. */
	return E_CAL_META_BACKEND_CLASS (e_cal_meta_backend_test_parent_class)->get_changes_sync (meta_backend,
		last_sync_tag, is_repeat, out_new_sync_tag, out_repeat, out_created_objects,
		out_modified_objects, out_removed_objects, cancellable, error);
}

static gboolean
e_cal_meta_backend_test_list_existing_sync (ECalMetaBackend *meta_backend,
					    gchar **out_new_sync_tag,
					    GSList **out_existing_objects,
					    GCancellable *cancellable,
					    GError **error)
{
	ECalMetaBackendTest *test_backend;
	ECalCache *cal_cache;
	icalcomponent *icalcomp;

	g_return_val_if_fail (E_IS_CAL_META_BACKEND_TEST (meta_backend), FALSE);
	g_return_val_if_fail (out_new_sync_tag, FALSE);
	g_return_val_if_fail (out_existing_objects, FALSE);

	test_backend = E_CAL_META_BACKEND_TEST (meta_backend);
	test_backend->list_count++;

	g_assert (test_backend->is_connected);

	cal_cache = e_cal_meta_backend_ref_cache (meta_backend);
	g_assert_nonnull (cal_cache);

	*out_existing_objects = NULL;

	for (icalcomp = icalcomponent_get_first_component (test_backend->vcalendar, ICAL_VEVENT_COMPONENT);
	     icalcomp;
	     icalcomp = icalcomponent_get_next_component (test_backend->vcalendar, ICAL_VEVENT_COMPONENT)) {
		const gchar *uid;
		gchar *revision;
		ECalMetaBackendInfo *nfo;

		/* Detached instances are stored together with the master object */
		if (icalcomponent_get_first_property (icalcomp, ICAL_RECURRENCEID_PROPERTY))
			continue;

		uid = icalcomponent_get_uid (icalcomp);
		revision = e_cal_cache_dup_component_revision (cal_cache, icalcomp);

		nfo = e_cal_meta_backend_info_new (uid, revision, NULL, NULL);
		*out_existing_objects = g_slist_prepend (*out_existing_objects, nfo);

		g_free (revision);
	}

	g_object_unref (cal_cache);

	return TRUE;
}

static gboolean
e_cal_meta_backend_test_save_component_sync (ECalMetaBackend *meta_backend,
					     gboolean overwrite_existing,
					     EConflictResolution conflict_resolution,
					     const GSList *instances,
					     const gchar *extra,
					     gchar **out_new_uid,
					     gchar **out_new_extra,
					     GCancellable *cancellable,
					     GError **error)
{
	ECalMetaBackendTest *test_backend;
	icalcomponent *icalcomp;
	const gchar *uid;
	GSList *link;

	g_return_val_if_fail (E_IS_CAL_META_BACKEND_TEST (meta_backend), FALSE);
	g_return_val_if_fail (instances != NULL, FALSE);
	g_return_val_if_fail (out_new_uid != NULL, FALSE);

	test_backend = E_CAL_META_BACKEND_TEST (meta_backend);
	test_backend->save_count++;

	g_assert (test_backend->is_connected);

	uid = icalcomponent_get_uid (e_cal_component_get_icalcomponent (instances->data));
	g_assert_nonnull (uid);

	for (icalcomp = icalcomponent_get_first_component (test_backend->vcalendar, ICAL_VEVENT_COMPONENT);
	     icalcomp;) {
		const gchar *server_uid;

		server_uid = icalcomponent_get_uid (icalcomp);
		g_assert_nonnull (server_uid);

		if (g_str_equal (server_uid, uid)) {
			if (!overwrite_existing) {
				g_propagate_error (error, e_data_cal_create_error (ObjectIdAlreadyExists, NULL));
				return FALSE;
			}

			icalcomponent_remove_component (test_backend->vcalendar, icalcomp);
			icalcomponent_free (icalcomp);

			icalcomp = icalcomponent_get_first_component (test_backend->vcalendar, ICAL_VEVENT_COMPONENT);
		} else {
			icalcomp = icalcomponent_get_next_component (test_backend->vcalendar, ICAL_VEVENT_COMPONENT);
		}
	}

	for (link = (GSList *) instances; link; link = g_slist_next (link)) {
		ECalComponent *comp = link->data;
		const gchar *comp_uid;

		icalcomp = e_cal_component_get_icalcomponent (comp);
		g_assert_nonnull (icalcomp);

		comp_uid = icalcomponent_get_uid (icalcomp);
		g_assert_cmpstr (uid, ==, comp_uid);

		icalcomponent_add_component (test_backend->vcalendar, icalcomponent_new_clone (icalcomp));
	}

	*out_new_uid = g_strdup (uid);

	return TRUE;
}

static gboolean
e_cal_meta_backend_test_load_component_sync (ECalMetaBackend *meta_backend,
					     const gchar *uid,
					     const gchar *extra,
					     icalcomponent **out_instances,
					     gchar **out_extra,
					     GCancellable *cancellable,
					     GError **error)
{
	ECalMetaBackendTest *test_backend;
	icalcomponent *icalcomp;

	g_return_val_if_fail (E_IS_CAL_META_BACKEND_TEST (meta_backend), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (out_instances != NULL, FALSE);
	g_return_val_if_fail (out_extra != NULL, FALSE);

	test_backend = E_CAL_META_BACKEND_TEST (meta_backend);
	test_backend->load_count++;

	g_assert (test_backend->is_connected);

	*out_instances = NULL;

	for (icalcomp = icalcomponent_get_first_component (test_backend->vcalendar, ICAL_VEVENT_COMPONENT);
	     icalcomp;
	     icalcomp = icalcomponent_get_next_component (test_backend->vcalendar, ICAL_VEVENT_COMPONENT)) {
		const gchar *server_uid;

		server_uid = icalcomponent_get_uid (icalcomp);
		g_assert_nonnull (server_uid);

		if (g_str_equal (server_uid, uid)) {
			if (!*out_instances)
				*out_instances = e_cal_util_new_top_level ();

			icalcomponent_add_component (*out_instances, icalcomponent_new_clone (icalcomp));
		}
	}

	if (*out_instances) {
		*out_extra = g_strconcat ("extra for ", uid, NULL);
		return TRUE;
	} else {
		g_propagate_error (error, e_data_cal_create_error (ObjectNotFound, NULL));
	}

	return FALSE;
}

static gboolean
e_cal_meta_backend_test_remove_component_sync (ECalMetaBackend *meta_backend,
					       EConflictResolution conflict_resolution,
					       const gchar *uid,
					       const gchar *extra,
					       const gchar *object,
					       GCancellable *cancellable,
					       GError **error)
{
	ECalMetaBackendTest *test_backend;
	icalcomponent *icalcomp;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_CAL_META_BACKEND_TEST (meta_backend), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (extra != NULL, FALSE);

	test_backend = E_CAL_META_BACKEND_TEST (meta_backend);
	test_backend->remove_count++;

	g_assert (test_backend->is_connected);

	for (icalcomp = icalcomponent_get_first_component (test_backend->vcalendar, ICAL_VEVENT_COMPONENT);
	     icalcomp;) {
		const gchar *server_uid;

		server_uid = icalcomponent_get_uid (icalcomp);
		g_assert_nonnull (server_uid);

		if (g_str_equal (server_uid, uid)) {
			if (!success) {
				gchar *expected_extra;

				expected_extra = g_strconcat ("extra for ", uid, NULL);
				g_assert_cmpstr (expected_extra, ==, extra);
				g_free (expected_extra);
			}

			success = TRUE;

			icalcomponent_remove_component (test_backend->vcalendar, icalcomp);
			icalcomponent_free (icalcomp);

			icalcomp = icalcomponent_get_first_component (test_backend->vcalendar, ICAL_VEVENT_COMPONENT);
		} else {
			icalcomp = icalcomponent_get_next_component (test_backend->vcalendar, ICAL_VEVENT_COMPONENT);
		}
	}

	if (!success)
		g_propagate_error (error, e_data_cal_create_error (ObjectNotFound, NULL));

	return success;
}

static void
e_cal_meta_backend_test_reset_counters (ECalMetaBackendTest *test_backend)
{
	g_return_if_fail (E_IS_CAL_META_BACKEND_TEST (test_backend));

	test_backend->connect_count = 0;
	test_backend->list_count = 0;
	test_backend->save_count = 0;
	test_backend->load_count = 0;
	test_backend->remove_count = 0;
}

static ECalCache *glob_use_cache = NULL;

static void
e_cal_meta_backend_test_constructed (GObject *object)
{
	ECalMetaBackendTest *test_backend = E_CAL_META_BACKEND_TEST (object);

	g_assert_nonnull (glob_use_cache);

	/* Set it before ECalMetaBackend::constucted() creates its own cache */
	e_cal_meta_backend_set_cache (E_CAL_META_BACKEND (test_backend), glob_use_cache);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_meta_backend_test_parent_class)->constructed (object);
}

static void
e_cal_meta_backend_test_finalize (GObject *object)
{
	ECalMetaBackendTest *test_backend = E_CAL_META_BACKEND_TEST (object);

	g_assert_nonnull (test_backend->vcalendar);

	icalcomponent_free (test_backend->vcalendar);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_meta_backend_test_parent_class)->finalize (object);
}

static void
e_cal_meta_backend_test_class_init (ECalMetaBackendTestClass *klass)
{
	ECalMetaBackendClass *cal_meta_backend_class;
	ECalBackendClass *cal_backend_class;
	GObjectClass *object_class;

	cal_meta_backend_class = E_CAL_META_BACKEND_CLASS (klass);
	cal_meta_backend_class->connect_sync = e_cal_meta_backend_test_connect_sync;
	cal_meta_backend_class->disconnect_sync = e_cal_meta_backend_test_disconnect_sync;
	cal_meta_backend_class->get_changes_sync = e_cal_meta_backend_test_get_changes_sync;
	cal_meta_backend_class->list_existing_sync = e_cal_meta_backend_test_list_existing_sync;
	cal_meta_backend_class->save_component_sync = e_cal_meta_backend_test_save_component_sync;
	cal_meta_backend_class->load_component_sync = e_cal_meta_backend_test_load_component_sync;
	cal_meta_backend_class->remove_component_sync = e_cal_meta_backend_test_remove_component_sync;

	cal_backend_class = E_CAL_BACKEND_CLASS (klass);
	cal_backend_class->get_backend_property = e_cal_meta_backend_test_get_backend_property;

	object_class = G_OBJECT_CLASS (klass);
	object_class->constructed = e_cal_meta_backend_test_constructed;
	object_class->finalize = e_cal_meta_backend_test_finalize;
}

static void
e_cal_meta_backend_test_init (ECalMetaBackendTest *test_backend)
{
	test_backend->sync_tag_index = 0;
	test_backend->is_connected = FALSE;
	test_backend->can_connect = TRUE;
	test_backend->vcalendar = e_cal_util_new_top_level ();

	e_cal_meta_backend_test_reset_counters (test_backend);

	e_backend_set_online (E_BACKEND (test_backend), TRUE);
	e_cal_backend_set_writable (E_CAL_BACKEND (test_backend), TRUE);

	ecmb_test_add_test_case (test_backend, "event-1");
	ecmb_test_add_test_case (test_backend, "event-2");
	ecmb_test_add_test_case (test_backend, "event-3");
	ecmb_test_add_test_case (test_backend, "event-4");
	ecmb_test_add_test_case (test_backend, "event-5");
	ecmb_test_add_test_case (test_backend, "event-6");
	ecmb_test_add_test_case (test_backend, "event-6-a");
	ecmb_test_add_test_case (test_backend, "event-7");
	ecmb_test_add_test_case (test_backend, "event-8");
	ecmb_test_add_test_case (test_backend, "event-9");
}

static ESourceRegistry *glob_registry = NULL;

static ECalMetaBackend *
e_cal_meta_backend_test_new (ECalCache *cache)
{
	ECalMetaBackend *meta_backend;
	ESource *scratch;
	gboolean success;
	GError *error = NULL;

	g_assert (E_IS_CAL_CACHE (cache));

	g_assert_nonnull (glob_registry);
	g_assert_null (glob_use_cache);

	glob_use_cache = cache;

	scratch = e_source_new_with_uid ("test-source", NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (scratch);

	meta_backend = g_object_new (E_TYPE_CAL_META_BACKEND_TEST,
		"source", scratch,
		"registry", glob_registry,
		"kind", ICAL_VEVENT_COMPONENT,
		NULL);
	g_assert_nonnull (meta_backend);

	g_assert (glob_use_cache == cache);
	glob_use_cache = NULL;

	g_object_unref (scratch);

	e_cal_meta_backend_set_cache (meta_backend, cache);

	#define set_extra_data(_uid, _rid) \
		success = e_cal_cache_set_component_extra (cache, _uid, _rid, "extra for " _uid, NULL, &error); \
		g_assert_no_error (error); \
		g_assert (success);

	set_extra_data ("event-1", NULL);
	set_extra_data ("event-2", NULL);
	set_extra_data ("event-3", NULL);
	set_extra_data ("event-4", NULL);
	set_extra_data ("event-5", NULL);
	set_extra_data ("event-6", NULL);
	set_extra_data ("event-6", "20170225T134900");
	set_extra_data ("event-7", NULL);
	set_extra_data ("event-8", NULL);
	set_extra_data ("event-9", NULL);

	#undef set_extra_data

	return meta_backend;
}

static void
e_cal_meta_backend_test_change_online (ECalMetaBackend *meta_backend,
				       gboolean is_online)
{
	EFlag *flag;
	gulong handler_id;

	if (!is_online) {
		e_backend_set_online (E_BACKEND (meta_backend), FALSE);
		return;
	}

	if (e_backend_get_online (E_BACKEND (meta_backend)))
		return;

	flag = e_flag_new ();

	handler_id = g_signal_connect_swapped (meta_backend, "refresh-completed",
		G_CALLBACK (e_flag_set), flag);

	/* Going online triggers refresh, thus wait for it */
	e_backend_set_online (E_BACKEND (meta_backend), TRUE);

	e_flag_wait (flag);
	e_flag_free (flag);

	g_signal_handler_disconnect (meta_backend, handler_id);
}

static void
e_cal_meta_backend_test_call_refresh (ECalMetaBackend *meta_backend)
{
	ECalBackendSyncClass *backend_class;
	EFlag *flag;
	gulong handler_id;
	GError *error = NULL;

	backend_class = E_CAL_BACKEND_SYNC_GET_CLASS (meta_backend);
	g_return_if_fail (backend_class != NULL);
	g_return_if_fail (backend_class->refresh_sync != NULL);

	if (!e_backend_get_online (E_BACKEND (meta_backend)))
		return;

	flag = e_flag_new ();

	handler_id = g_signal_connect_swapped (meta_backend, "refresh-completed",
		G_CALLBACK (e_flag_set), flag);

	backend_class->refresh_sync (E_CAL_BACKEND_SYNC (meta_backend), NULL, NULL, &error);
	g_assert_no_error (error);

	e_flag_wait (flag);
	e_flag_free (flag);

	g_signal_handler_disconnect (meta_backend, handler_id);
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

static void
test_empty_cache (TCUFixture *fixture,
		  gconstpointer user_data)
{
	#define TZID "/meta/backend/test/timezone"
	#define TZLOC "test/timezone"

	const gchar *in_vcalobj =
		"BEGIN:VCALENDAR\r\n"
		"BEGIN:VTIMEZONE\r\n"
		"TZID:" TZID "\r\n"
		"X-LIC-LOCATION:" TZLOC "\r\n"
		"BEGIN:STANDARD\r\n"
		"TZNAME:Test-ST\r\n"
		"DTSTART:19701106T020000\r\n"
		"RRULE:FREQ=YEARLY;BYDAY=1SU;BYMONTH=11\r\n"
		"TZOFFSETFROM:-0400\r\n"
		"TZOFFSETTO:-0500\r\n"
		"END:STANDARD\r\n"
		"BEGIN:DAYLIGHT\r\n"
		"TZNAME:Test-DT\r\n"
		"DTSTART:19700313T020000\r\n"
		"RRULE:FREQ=YEARLY;BYDAY=2SU;BYMONTH=3\r\n"
		"TZOFFSETFROM:-0500\r\n"
		"TZOFFSETTO:-0400\r\n"
		"END:DAYLIGHT\r\n"
		"END:VTIMEZONE\r\n"
		"BEGIN:VEVENT\r\n"
		"UID:test-event\r\n"
		"DTSTAMP:20170130T000000Z\r\n"
		"CREATED:20170216T155507Z\r\n"
		"LAST-MODIFIED:20170216T155543Z\r\n"
		"SEQUENCE:1\r\n"
		"DTSTART;TZID=" TZID ":20170209T013000Z\r\n"
		"DTEND;TZID=" TZID ":20170209T030000Z\r\n"
		"SUMMARY:Test Event\r\n"
		"END:VEVENT\r\n"
		"END:VCALENDAR\r\n";
	ECalBackendSyncClass *backend_class;
	ECalMetaBackend *meta_backend;
	GList *zones;
	gboolean success;
	GError *error = NULL;

	meta_backend = e_cal_meta_backend_test_new (fixture->cal_cache);
	g_assert_nonnull (meta_backend);

	backend_class = E_CAL_BACKEND_SYNC_GET_CLASS (meta_backend);
	g_return_if_fail (backend_class != NULL);
	g_return_if_fail (backend_class->receive_objects_sync != NULL);

	/* This adds the object and the used timezone to the cache */
	backend_class->receive_objects_sync (E_CAL_BACKEND_SYNC (meta_backend), NULL, NULL, in_vcalobj, &error);
	g_assert_no_error (error);

	zones = NULL;
	success = e_cal_cache_list_timezones (fixture->cal_cache, &zones, NULL, &error);
	g_assert_no_error (error);
	g_assert (success);
	g_assert_cmpint (g_list_length (zones), ==, 1);
	g_list_free (zones);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->cal_cache), E_CACHE_INCLUDE_DELETED, NULL, &error), >, 0);
	g_assert_no_error (error);

	/* Empty the cache */
	success = e_cal_meta_backend_empty_cache_sync (meta_backend, NULL, &error);
	g_assert_no_error (error);
	g_assert (success);

	/* Verify the cache is truly empty */
	zones = NULL;
	success = e_cal_cache_list_timezones (fixture->cal_cache, &zones, NULL, &error);
	g_assert_no_error (error);
	g_assert (success);
	g_assert_cmpint (g_list_length (zones), ==, 0);
	g_list_free (zones);

	g_assert_cmpint (e_cache_get_count (E_CACHE (fixture->cal_cache), E_CACHE_INCLUDE_DELETED, NULL, &error), ==, 0);
	g_assert_no_error (error);

	g_object_unref (meta_backend);

	#undef TZID
	#undef TZLOC
}

static void
test_send_objects (ECalMetaBackend *meta_backend)
{
	ECalBackendSyncClass *backend_class;
	GSList *users = NULL;
	const gchar *calobj = "fake-iCalendar-object";
	gchar *modified_calobj = NULL;
	GError *error = NULL;

	g_assert_nonnull (meta_backend);

	backend_class = E_CAL_BACKEND_SYNC_GET_CLASS (meta_backend);
	g_return_if_fail (backend_class != NULL);
	g_return_if_fail (backend_class->send_objects_sync != NULL);

	backend_class->send_objects_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, calobj, &users, &modified_calobj, &error);

	g_assert_no_error (error);
	g_assert_null (users);
	g_assert_cmpstr (calobj, ==, modified_calobj);

	g_free (modified_calobj);
}

static void
test_get_attachment_uris (ECalMetaBackend *meta_backend)
{
	ECalBackendSyncClass *backend_class;
	gchar *expected_uri, *filename;
	GSList *uris = NULL;
	GError *error = NULL;

	g_assert_nonnull (meta_backend);

	backend_class = E_CAL_BACKEND_SYNC_GET_CLASS (meta_backend);
	g_return_if_fail (backend_class != NULL);
	g_return_if_fail (backend_class->get_attachment_uris_sync != NULL);

	/* non-existent event */
	backend_class->get_attachment_uris_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, "unknown-event", NULL, &uris, &error);

	g_assert_error (error, E_DATA_CAL_ERROR, ObjectNotFound);
	g_assert_null (uris);

	g_clear_error (&error);

	/* existent event, but with no attachments */
	backend_class->get_attachment_uris_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, "event-1", NULL, &uris, &error);

	g_assert_no_error (error);
	g_assert_null (uris);

	/* event with attachments */
	backend_class->get_attachment_uris_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, "event-7", NULL, &uris, &error);

	g_assert_no_error (error);
	g_assert_nonnull (uris);
	g_assert_cmpint (g_slist_length (uris), ==, 1);

	filename = tcu_get_test_case_filename ("event-1");
	expected_uri = g_filename_to_uri (filename, NULL, NULL);
	g_free (filename);

	g_assert_cmpstr (uris->data, ==, expected_uri);
	g_free (expected_uri);

	g_slist_free_full (uris, g_free);
}

static void
test_discard_alarm (ECalMetaBackend *meta_backend)
{
	ECalBackendSyncClass *backend_class;
	GError *error = NULL;

	g_assert_nonnull (meta_backend);

	backend_class = E_CAL_BACKEND_SYNC_GET_CLASS (meta_backend);
	g_return_if_fail (backend_class != NULL);
	g_return_if_fail (backend_class->discard_alarm_sync != NULL);

	/* Not implemented */
	backend_class->discard_alarm_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, "unknown-event", NULL, NULL, &error);

	g_assert_error (error, E_CLIENT_ERROR, E_CLIENT_ERROR_NOT_SUPPORTED);

	g_clear_error (&error);
}

static gboolean
tcmb_get_uint64_cb (ECache *cache,
		    gint ncols,
		    const gchar **column_names,
		    const gchar **column_values,
		    gpointer user_data)
{
	guint64 *pui64 = user_data;

	g_return_val_if_fail (pui64 != NULL, FALSE);

	if (ncols == 1) {
		*pui64 = column_values[0] ? g_ascii_strtoull (column_values[0], NULL, 10) : 0;
	} else {
		*pui64 = 0;
	}

	return TRUE;
}

static gint
tcmb_get_tzid_ref_count (ECalCache *cal_cache,
			 const gchar *tzid)
{
	guint64 refs = 0;
	gchar *stmt;
	gboolean success;
	GError *error = NULL;

	g_return_val_if_fail (E_IS_CAL_CACHE (cal_cache), -1);
	g_return_val_if_fail (tzid != NULL, -1);

	stmt = e_cache_sqlite_stmt_printf ("SELECT refs FROM timezones WHERE tzid=%Q", tzid);

	success = e_cache_sqlite_select (E_CACHE (cal_cache), stmt, tcmb_get_uint64_cb, &refs, NULL, &error);
	g_assert_no_error (error);
	g_assert (success);

	e_cache_sqlite_stmt_free (stmt);

	return (gint) refs;
}

static void
test_timezones (ECalMetaBackend *meta_backend)
{
	#define TZID1 "/meta/backend/test/timezone1"
	#define TZLOC1 "test/timezone1"
	#define TZID2 "/meta/backend/test/timezone2"
	#define TZLOC2 "test/timezone2"
	#define TZSTRDEF(id, loc) \
		"BEGIN:VTIMEZONE\r\n" \
		"TZID:" id "\r\n" \
		"X-LIC-LOCATION:" loc "\r\n" \
		"BEGIN:STANDARD\r\n" \
		"TZNAME:Test-ST\r\n" \
		"DTSTART:19701106T020000\r\n" \
		"RRULE:FREQ=YEARLY;BYDAY=1SU;BYMONTH=11\r\n" \
		"TZOFFSETFROM:-0400\r\n" \
		"TZOFFSETTO:-0500\r\n" \
		"END:STANDARD\r\n" \
		"BEGIN:DAYLIGHT\r\n" \
		"TZNAME:Test-DT\r\n" \
		"DTSTART:19700313T020000\r\n" \
		"RRULE:FREQ=YEARLY;BYDAY=2SU;BYMONTH=3\r\n" \
		"TZOFFSETFROM:-0500\r\n" \
		"TZOFFSETTO:-0400\r\n" \
		"END:DAYLIGHT\r\n" \
		"END:VTIMEZONE\r\n"

	const gchar *in_tz1obj = TZSTRDEF (TZID1, TZLOC1);
	const gchar *in_tz2obj = TZSTRDEF (TZID2, TZLOC2);
	ECalBackendSyncClass *backend_class;
	ECalCache *cal_cache;
	icalcomponent *vcalendar;
	ECalComponent *comp;
	gchar *tzobj = NULL;
	GList *zones;
	gboolean success;
	GError *error = NULL;

	g_assert_nonnull (meta_backend);

	backend_class = E_CAL_BACKEND_SYNC_GET_CLASS (meta_backend);
	g_return_if_fail (backend_class != NULL);
	g_return_if_fail (backend_class->add_timezone_sync != NULL);
	g_return_if_fail (backend_class->get_timezone_sync != NULL);
	g_return_if_fail (backend_class->get_timezone_sync != NULL);

	/* Verify neither TZID, not LOCATION is in the timezone cache */
	backend_class->get_timezone_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, TZID1, &tzobj, &error);
	g_assert_error (error, E_DATA_CAL_ERROR, ObjectNotFound);
	g_assert_null (tzobj);
	g_clear_error (&error);

	backend_class->get_timezone_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, TZLOC1, &tzobj, &error);
	g_assert_error (error, E_DATA_CAL_ERROR, ObjectNotFound);
	g_assert_null (tzobj);
	g_clear_error (&error);

	/* Add it to the cache */
	backend_class->add_timezone_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, in_tz1obj, &error);
	g_assert_no_error (error);

	/* Read it back */
	backend_class->get_timezone_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, TZID1, &tzobj, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (tzobj, ==, in_tz1obj);
	g_free (tzobj);
	tzobj = NULL;

	/* As a non-built-in timezone it cannot be read with location, only with TZID */
	backend_class->get_timezone_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, TZLOC1, &tzobj, &error);
	g_assert_error (error, E_DATA_CAL_ERROR, ObjectNotFound);
	g_assert_null (tzobj);
	g_clear_error (&error);

	/* Try also internal timezone, which will be renamed and added to the cache too */
	backend_class->get_timezone_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, "America/New_York", &tzobj, &error);
	g_assert_no_error (error);
	g_assert_nonnull (tzobj);
	g_assert (strstr (tzobj, "America/New_York") != NULL);
	g_free (tzobj);
	tzobj = NULL;

	cal_cache = e_cal_meta_backend_ref_cache (meta_backend);
	g_assert_nonnull (cal_cache);

	vcalendar = icalcomponent_new_from_string (
		"BEGIN:VCALENDAR\r\n"
		"BEGIN:VTIMEZONE\r\n"
		"TZID:tzid1\r\n"
		"X-LIC-LOCATION:tzid/1\r\n"
		"BEGIN:STANDARD\r\n"
		"TZNAME:Test-ST\r\n"
		"DTSTART:19701106T020000\r\n"
		"RRULE:FREQ=YEARLY;BYDAY=1SU;BYMONTH=11\r\n"
		"TZOFFSETFROM:-0400\r\n"
		"TZOFFSETTO:-0500\r\n"
		"END:STANDARD\r\n"
		"BEGIN:DAYLIGHT\r\n"
		"TZNAME:Test-DT\r\n"
		"DTSTART:19700313T020000\r\n"
		"RRULE:FREQ=YEARLY;BYDAY=2SU;BYMONTH=3\r\n"
		"TZOFFSETFROM:-0500\r\n"
		"TZOFFSETTO:-0400\r\n"
		"END:DAYLIGHT\r\n"
		"END:VTIMEZONE\r\n"
		"BEGIN:VTIMEZONE\r\n"
		"TZID:tzid2\r\n"
		"X-LIC-LOCATION:tzid/2\r\n"
		"BEGIN:STANDARD\r\n"
		"TZNAME:Test-ST\r\n"
		"DTSTART:19701106T020000\r\n"
		"RRULE:FREQ=YEARLY;BYDAY=1SU;BYMONTH=11\r\n"
		"TZOFFSETFROM:-0400\r\n"
		"TZOFFSETTO:-0500\r\n"
		"END:STANDARD\r\n"
		"BEGIN:DAYLIGHT\r\n"
		"TZNAME:Test-DT\r\n"
		"DTSTART:19700313T020000\r\n"
		"RRULE:FREQ=YEARLY;BYDAY=2SU;BYMONTH=3\r\n"
		"TZOFFSETFROM:-0500\r\n"
		"TZOFFSETTO:-0400\r\n"
		"END:DAYLIGHT\r\n"
		"END:VTIMEZONE\r\n"
		"BEGIN:VEVENT\r\n"
		"UID:test-event\r\n"
		"DTSTAMP:20170130T000000Z\r\n"
		"CREATED:20170216T155507Z\r\n"
		"LAST-MODIFIED:20170216T155543Z\r\n"
		"SEQUENCE:1\r\n"
		"DTSTART:20170209T013000Z\r\n"
		"DTEND:20170209T030000Z\r\n"
		"SUMMARY:Test Event\r\n"
		"END:VEVENT\r\n"
		"END:VCALENDAR\r\n");
	g_assert_nonnull (vcalendar);

	zones = NULL;
	success = e_cal_cache_list_timezones (cal_cache, &zones, NULL, &error);
	g_assert_no_error (error);
	g_assert (success);
	g_assert_cmpint (g_list_length (zones), ==, 0);
	g_list_free (zones);

	zones = e_timezone_cache_list_timezones (E_TIMEZONE_CACHE (meta_backend));
	g_assert_cmpint (g_list_length (zones), ==, 2);
	g_list_free (zones);

	/* Merge with existing */
	success = e_cal_meta_backend_gather_timezones_sync (meta_backend, vcalendar, FALSE, NULL, &error);
	g_assert_no_error (error);
	g_assert (success);

	zones = NULL;
	success = e_cal_cache_list_timezones (cal_cache, &zones, NULL, &error);
	g_assert_no_error (error);
	g_assert (success);
	g_assert_cmpint (g_list_length (zones), ==, 0);
	g_list_free (zones);

	zones = e_timezone_cache_list_timezones (E_TIMEZONE_CACHE (meta_backend));
	g_assert_cmpint (g_list_length (zones), ==, 4);
	g_list_free (zones);

	success = e_cal_cache_remove_timezones (cal_cache, NULL, &error);
	g_assert_no_error (error);
	g_assert (success);

	_e_cal_cache_remove_loaded_timezones (cal_cache);
	_e_cal_backend_remove_cached_timezones (E_CAL_BACKEND (meta_backend));

	zones = e_timezone_cache_list_timezones (E_TIMEZONE_CACHE (meta_backend));
	g_assert_cmpint (g_list_length (zones), ==, 0);

	backend_class->add_timezone_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, in_tz1obj, &error);
	g_assert_no_error (error);

	zones = e_timezone_cache_list_timezones (E_TIMEZONE_CACHE (meta_backend));
	g_assert_cmpint (g_list_length (zones), ==, 1);
	g_list_free (zones);

	/* Remove existing and add the new */
	success = e_cal_meta_backend_gather_timezones_sync (meta_backend, vcalendar, TRUE, NULL, &error);
	g_assert_no_error (error);
	g_assert (success);

	_e_cal_cache_remove_loaded_timezones (cal_cache);
	_e_cal_backend_remove_cached_timezones (E_CAL_BACKEND (meta_backend));

	zones = e_timezone_cache_list_timezones (E_TIMEZONE_CACHE (meta_backend));
	g_assert_cmpint (g_list_length (zones), ==, 0);

	icalcomponent_free (vcalendar);

	/* And now when the timezones are actually referenced, thus should be part of the persistent cache */

	backend_class->add_timezone_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, in_tz1obj, &error);
	g_assert_no_error (error);

	backend_class->add_timezone_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, in_tz2obj, &error);
	g_assert_no_error (error);

	zones = NULL;
	success = e_cal_cache_list_timezones (cal_cache, &zones, NULL, &error);
	g_assert_no_error (error);
	g_assert (success);
	g_assert_cmpint (g_list_length (zones), ==, 0);

	/* Uses TZID1 twice */
	comp = e_cal_component_new_from_string (
		"BEGIN:VEVENT\r\n"
		"UID:tz1\r\n"
		"DTSTAMP:20170130T000000Z\r\n"
		"CREATED:20170216T155507Z\r\n"
		"LAST-MODIFIED:20170216T155543Z\r\n"
		"SEQUENCE:1\r\n"
		"DTSTART;TZID=" TZID1 ":20170209T013000\r\n"
		"DTEND;TZID=" TZID1 ":20170209T030000\r\n"
		"SUMMARY:tz1\r\n"
		"END:VEVENT\r\n");
	g_assert_nonnull (comp);

	/* Add a component which uses TZID1, thus it's in the cache */
	success = e_cal_cache_put_component (cal_cache, comp, NULL, E_CACHE_IS_ONLINE, NULL, &error);
	g_assert_no_error (error);
	g_assert (success);

	g_object_unref (comp);
	_e_cal_cache_remove_loaded_timezones (cal_cache);

	zones = NULL;
	success = e_cal_cache_list_timezones (cal_cache, &zones, NULL, &error);
	g_assert_no_error (error);
	g_assert (success);
	g_assert_cmpint (g_list_length (zones), ==, 1);
	g_list_free (zones);

	g_assert_cmpint (tcmb_get_tzid_ref_count (cal_cache, TZID1), ==, 2);

	/* Uses TZID1 and TZID2 */
	comp = e_cal_component_new_from_string (
		"BEGIN:VEVENT\r\n"
		"UID:tz2\r\n"
		"DTSTAMP:20170130T000000Z\r\n"
		"CREATED:20170216T155507Z\r\n"
		"LAST-MODIFIED:20170216T155543Z\r\n"
		"SEQUENCE:1\r\n"
		"DTSTART;TZID=" TZID2 ":20170209T013000\r\n"
		"DTEND;TZID=" TZID1 ":20170209T030000\r\n"
		"SUMMARY:tz2\r\n"
		"END:VEVENT\r\n");
	g_assert_nonnull (comp);

	backend_class->add_timezone_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, in_tz2obj, &error);
	g_assert_no_error (error);

	/* Add a component which uses TZID1 and TZID2, thus it's in the cache */
	success = e_cal_cache_put_component (cal_cache, comp, NULL, E_CACHE_IS_ONLINE, NULL, &error);
	g_assert_no_error (error);
	g_assert (success);

	g_object_unref (comp);
	_e_cal_cache_remove_loaded_timezones (cal_cache);

	zones = NULL;
	success = e_cal_cache_list_timezones (cal_cache, &zones, NULL, &error);
	g_assert_no_error (error);
	g_assert (success);
	g_assert_cmpint (g_list_length (zones), ==, 2);
	g_list_free (zones);

	g_assert_cmpint (tcmb_get_tzid_ref_count (cal_cache, TZID1), ==, 3);
	g_assert_cmpint (tcmb_get_tzid_ref_count (cal_cache, TZID2), ==, 1);

	/* Remove in offline doesn't modify timezone cache, because the component is still there */
	success = e_cal_cache_remove_component (cal_cache, "tz1", NULL, E_CACHE_IS_OFFLINE, NULL, &error);
	g_assert_no_error (error);
	g_assert (success);

	g_assert_cmpint (tcmb_get_tzid_ref_count (cal_cache, TZID1), ==, 3);
	g_assert_cmpint (tcmb_get_tzid_ref_count (cal_cache, TZID2), ==, 1);

	/* Remove in online modifies timezone cache */
	success = e_cal_cache_remove_component (cal_cache, "tz1", NULL, E_CACHE_IS_ONLINE, NULL, &error);
	g_assert_no_error (error);
	g_assert (success);

	_e_cal_cache_remove_loaded_timezones (cal_cache);

	zones = NULL;
	success = e_cal_cache_list_timezones (cal_cache, &zones, NULL, &error);
	g_assert_no_error (error);
	g_assert (success);
	g_assert_cmpint (g_list_length (zones), ==, 2);
	g_list_free (zones);

	g_assert_cmpint (tcmb_get_tzid_ref_count (cal_cache, TZID1), ==, 1);
	g_assert_cmpint (tcmb_get_tzid_ref_count (cal_cache, TZID2), ==, 1);

	/* Modify tz2 to use only TZID2, TZID1 is removed */
	comp = e_cal_component_new_from_string (
		"BEGIN:VEVENT\r\n"
		"UID:tz2\r\n"
		"DTSTAMP:20170130T000000Z\r\n"
		"CREATED:20170216T155507Z\r\n"
		"LAST-MODIFIED:20170216T155544Z\r\n"
		"SEQUENCE:2\r\n"
		"DTSTART;TZID=" TZID2 ":20170209T013000\r\n"
		"DTEND:20170209T030000Z\r\n"
		"SUMMARY:tz2\r\n"
		"END:VEVENT\r\n");
	g_assert_nonnull (comp);

	success = e_cal_cache_put_component (cal_cache, comp, NULL, E_CACHE_IS_ONLINE, NULL, &error);
	g_assert_no_error (error);
	g_assert (success);

	g_object_unref (comp);
	_e_cal_cache_remove_loaded_timezones (cal_cache);

	zones = NULL;
	success = e_cal_cache_list_timezones (cal_cache, &zones, NULL, &error);
	g_assert_no_error (error);
	g_assert (success);
	g_assert_cmpint (g_list_length (zones), ==, 1);
	g_list_free (zones);

	g_assert_cmpint (tcmb_get_tzid_ref_count (cal_cache, TZID1), ==, 0);
	g_assert_cmpint (tcmb_get_tzid_ref_count (cal_cache, TZID2), ==, 1);

	/* Finally remove component straight in online, which removed the only one timezone too */
	success = e_cal_cache_remove_component (cal_cache, "tz2", NULL, E_CACHE_IS_ONLINE, NULL, &error);
	g_assert_no_error (error);
	g_assert (success);

	_e_cal_cache_remove_loaded_timezones (cal_cache);

	zones = NULL;
	success = e_cal_cache_list_timezones (cal_cache, &zones, NULL, &error);
	g_assert_no_error (error);
	g_assert (success);
	g_assert_cmpint (g_list_length (zones), ==, 0);

	g_assert_cmpint (tcmb_get_tzid_ref_count (cal_cache, TZID1), ==, 0);
	g_assert_cmpint (tcmb_get_tzid_ref_count (cal_cache, TZID2), ==, 0);

	g_object_unref (cal_cache);

	#undef TZSTRDEF
	#undef TZLOC2
	#undef TZID2
	#undef TZLOC1
	#undef TZID1
}

static void
test_get_free_busy (ECalMetaBackend *meta_backend)
{
	const gchar *expected_fbobj =
		"BEGIN:VFREEBUSY\r\n"
		"ORGANIZER:mailto:user@no.where\r\n"
		"DTSTART:20170102T080000Z\r\n"
		"DTEND:20170102T200000Z\r\n"
		"FREEBUSY;FBTYPE=BUSY;X-SUMMARY=After-party clean up;X-LOCATION=All around:\r\n"
		" 20170102T100000Z/20170102T180000Z\r\n"
		"END:VFREEBUSY\r\n";
	ECalBackendSyncClass *backend_class;
	GSList *users, *objects = NULL;
	time_t start, end;
	GError *error = NULL;

	g_assert_nonnull (meta_backend);

	backend_class = E_CAL_BACKEND_SYNC_GET_CLASS (meta_backend);
	g_return_if_fail (backend_class != NULL);
	g_return_if_fail (backend_class->get_free_busy_sync != NULL);

	users = g_slist_prepend (NULL, (gpointer) "user@no.where");
	users = g_slist_prepend (users, (gpointer) "unknown@no.where");

	start = icaltime_as_timet (icaltime_from_string ("20170102T080000Z"));
	end = icaltime_as_timet (icaltime_from_string ("20170102T200000Z"));

	backend_class->get_free_busy_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, users, start, end, &objects, &error);

	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (objects), ==, 1);
	g_assert_cmpstr (objects->data, ==, expected_fbobj);

	g_slist_free_full (objects, g_free);
	g_slist_free (users);
}

static void
test_create_objects (ECalMetaBackend *meta_backend)
{
	ECalMetaBackendTest *test_backend;
	ECalBackendSyncClass *backend_class;
	ECalCache *cal_cache;
	GSList *objects, *uids = NULL, *new_components = NULL, *offline_changes;
	gchar *calobj, *tmp;
	GError *error = NULL;

	g_assert_nonnull (meta_backend);

	backend_class = E_CAL_BACKEND_SYNC_GET_CLASS (meta_backend);
	g_return_if_fail (backend_class != NULL);
	g_return_if_fail (backend_class->create_objects_sync != NULL);

	test_backend = E_CAL_META_BACKEND_TEST (meta_backend);
	cal_cache = e_cal_meta_backend_ref_cache (meta_backend);
	g_assert_nonnull (cal_cache);

	/* Prepare cache and server content */
	e_cal_cache_remove_component (cal_cache, "event-7", NULL, E_CACHE_IS_ONLINE, NULL, &error);
	g_assert_no_error (error);
	e_cal_cache_remove_component (cal_cache, "event-8", NULL, E_CACHE_IS_ONLINE, NULL, &error);
	g_assert_no_error (error);
	e_cal_cache_remove_component (cal_cache, "event-9", NULL, E_CACHE_IS_ONLINE, NULL, &error);
	g_assert_no_error (error);

	ecmb_test_remove_component (test_backend, "event-7", NULL);
	ecmb_test_remove_component (test_backend, "event-8", NULL);
	ecmb_test_remove_component (test_backend, "event-9", NULL);

	ecmb_test_cache_and_server_equal (cal_cache, test_backend->vcalendar, E_CACHE_INCLUDE_DELETED);

	/* Try to add existing event, it should fail */
	objects = g_slist_prepend (NULL, tcu_new_icalstring_from_test_case ("event-1"));

	backend_class->create_objects_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, objects, &uids, &new_components, &error);
	g_assert_error (error, E_DATA_CAL_ERROR, ObjectIdAlreadyExists);
	g_assert_null (uids);
	g_assert_null (new_components);
	g_clear_error (&error);
	g_slist_free_full (objects, g_free);

	e_cal_meta_backend_test_reset_counters (test_backend);

	/* Try to add new event */
	objects = g_slist_prepend (NULL, tcu_new_icalstring_from_test_case ("event-7"));

	backend_class->create_objects_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, objects, &uids, &new_components, &error);
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (uids), ==, 1);
	g_assert_cmpstr (uids->data, ==, "event-7");
	g_assert_cmpint (g_slist_length (new_components), ==, 1);
	g_assert_cmpint (test_backend->connect_count, ==, 1);
	g_assert_cmpint (test_backend->list_count, ==, 0);
	g_assert_cmpint (test_backend->load_count, ==, 1);
	g_assert_cmpint (test_backend->save_count, ==, 1);

	g_slist_free_full (uids, g_free);
	g_slist_free_full (new_components, g_object_unref);
	g_slist_free_full (objects, g_free);
	uids = NULL;
	new_components = NULL;

	ecmb_test_cache_and_server_equal (cal_cache, test_backend->vcalendar, E_CACHE_INCLUDE_DELETED);

	/* Going offline */
	e_cal_meta_backend_test_change_online (meta_backend, FALSE);

	e_cal_meta_backend_test_reset_counters (test_backend);

	/* Try to add existing event, it should fail */
	objects = g_slist_prepend (NULL, tcu_new_icalstring_from_test_case ("event-7"));

	backend_class->create_objects_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, objects, &uids, &new_components, &error);
	g_assert_error (error, E_DATA_CAL_ERROR, ObjectIdAlreadyExists);
	g_assert_null (uids);
	g_assert_null (new_components);
	g_clear_error (&error);
	g_slist_free_full (objects, g_free);
	g_assert_cmpint (test_backend->load_count, ==, 0);
	g_assert_cmpint (test_backend->save_count, ==, 0);

	/* Try to add new event */
	objects = g_slist_prepend (NULL, tcu_new_icalstring_from_test_case ("event-8"));

	backend_class->create_objects_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, objects, &uids, &new_components, &error);
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (uids), ==, 1);
	g_assert_cmpstr (uids->data, ==, "event-8");
	g_assert_cmpint (g_slist_length (new_components), ==, 1);
	g_assert_cmpint (test_backend->connect_count, ==, 0);
	g_assert_cmpint (test_backend->load_count, ==, 0);
	g_assert_cmpint (test_backend->save_count, ==, 0);

	g_slist_free_full (uids, g_free);
	g_slist_free_full (new_components, g_object_unref);
	g_slist_free_full (objects, g_free);
	uids = NULL;
	new_components = NULL;

	ecmb_test_vcalendar_contains (test_backend->vcalendar, TRUE, FALSE,
		"event-8", NULL, NULL);
	ecmb_test_cache_contains (cal_cache, FALSE, FALSE,
		"event-8", NULL, NULL);

	/* Going online */
	e_cal_meta_backend_test_change_online (meta_backend, TRUE);

	g_assert_cmpint (test_backend->connect_count, ==, 1);
	g_assert_cmpint (test_backend->load_count, ==, 1);
	g_assert_cmpint (test_backend->save_count, ==, 1);

	ecmb_test_cache_and_server_equal (cal_cache, test_backend->vcalendar, E_CACHE_INCLUDE_DELETED);

	offline_changes = e_cal_cache_get_offline_changes (cal_cache, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpint (0, ==, g_slist_length (offline_changes));

	/* Add event without UID */
	calobj = tcu_new_icalstring_from_test_case ("event-9");
	g_assert_nonnull (calobj);
	tmp = strstr (calobj, "UID:event-9\r\n");
	g_assert_nonnull (tmp);
	memcpy (tmp, "X-TEST:*007", 11);

	objects = g_slist_prepend (NULL, calobj);

	backend_class->create_objects_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, objects, &uids, &new_components, &error);
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (uids), ==, 1);
	g_assert_cmpstr (uids->data, !=, "event-9");
	g_assert_cmpint (g_slist_length (new_components), ==, 1);
	g_assert_cmpint (test_backend->connect_count, ==, 1);
	g_assert_cmpint (test_backend->list_count, ==, 1);
	g_assert_cmpint (test_backend->load_count, ==, 2);
	g_assert_cmpint (test_backend->save_count, ==, 2);

	calobj = e_cal_component_get_as_string (new_components->data);
	g_assert_nonnull (calobj);
	g_assert_nonnull (strstr (calobj, "X-TEST:*007\r\n"));
	g_assert_nonnull (strstr (calobj, uids->data));

	g_slist_free_full (uids, g_free);
	g_slist_free_full (new_components, g_object_unref);
	g_slist_free_full (objects, g_free);
	g_free (calobj);
	uids = NULL;
	new_components = NULL;

	ecmb_test_cache_and_server_equal (cal_cache, test_backend->vcalendar, E_CACHE_INCLUDE_DELETED);

	g_object_unref (cal_cache);
}

static gchar *
ecmb_test_modify_case (const gchar *case_name,
		       const gchar *ridstr)
{
	gchar *calobj;
	icalcomponent *icalcomp;

	g_assert_nonnull (case_name);

	calobj = tcu_new_icalstring_from_test_case (case_name);
	g_assert_nonnull (calobj);
	icalcomp = icalcomponent_new_from_string (calobj);
	g_assert_nonnull (icalcomp);
	g_free (calobj);

	icalcomponent_set_summary (icalcomp, MODIFIED_SUMMARY_STR);
	icalcomponent_set_sequence (icalcomp, icalcomponent_get_sequence (icalcomp) + 1);

	if (ridstr)
		icalcomponent_set_recurrenceid (icalcomp, icaltime_from_string (ridstr));

	calobj = icalcomponent_as_ical_string_r (icalcomp);
	icalcomponent_free (icalcomp);

	return calobj;
}

static void
test_modify_objects (ECalMetaBackend *meta_backend)
{
	ECalMetaBackendTest *test_backend;
	ECalBackendSyncClass *backend_class;
	ECalCache *cal_cache;
	GSList *objects, *old_components = NULL, *new_components = NULL, *offline_changes;
	gchar *calobj, *tmp;
	icalcomponent *icalcomp;
	gint old_sequence;
	GError *error = NULL;

	g_assert_nonnull (meta_backend);

	backend_class = E_CAL_BACKEND_SYNC_GET_CLASS (meta_backend);
	g_return_if_fail (backend_class != NULL);
	g_return_if_fail (backend_class->modify_objects_sync != NULL);

	test_backend = E_CAL_META_BACKEND_TEST (meta_backend);
	cal_cache = e_cal_meta_backend_ref_cache (meta_backend);
	g_assert_nonnull (cal_cache);

	/* Modify non-existing event */
	calobj = tcu_new_icalstring_from_test_case ("event-1");
	g_assert_nonnull (calobj);
	tmp = strstr (calobj, "UID:event-1");
	g_assert_nonnull (tmp);
	memcpy (tmp + 4, "unknown", 7);

	objects = g_slist_prepend (NULL, calobj);

	backend_class->modify_objects_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, objects, E_CAL_OBJ_MOD_ALL, &old_components, &new_components, &error);
	g_assert_error (error, E_DATA_CAL_ERROR, ObjectNotFound);
	g_assert_null (old_components);
	g_assert_null (new_components);
	g_clear_error (&error);
	g_slist_free_full (objects, g_free);

	/* Modify existing event */
	objects = g_slist_prepend (NULL, ecmb_test_modify_case ("event-1", NULL));

	backend_class->modify_objects_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, objects, E_CAL_OBJ_MOD_ALL, &old_components, &new_components, &error);
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (old_components), ==, 1);
	g_assert_cmpint (g_slist_length (new_components), ==, 1);
	g_assert_cmpint (test_backend->load_count, ==, 1);
	g_assert_cmpint (test_backend->save_count, ==, 1);

	icalcomp = e_cal_component_get_icalcomponent (old_components->data);
	old_sequence = icalcomponent_get_sequence (icalcomp);
	g_assert_cmpstr (icalcomponent_get_summary (icalcomp), !=, MODIFIED_SUMMARY_STR);
	g_assert_cmpstr (icalcomponent_get_uid (icalcomp), ==, "event-1");

	icalcomp = e_cal_component_get_icalcomponent (new_components->data);
	g_assert_cmpint (old_sequence + 1, ==, icalcomponent_get_sequence (icalcomp));
	g_assert_cmpstr (icalcomponent_get_summary (icalcomp), ==, MODIFIED_SUMMARY_STR);
	g_assert_cmpstr (icalcomponent_get_uid (icalcomp), ==, "event-1");

	g_slist_free_full (old_components, g_object_unref);
	g_slist_free_full (new_components, g_object_unref);
	g_slist_free_full (objects, g_free);
	old_components = NULL;
	new_components = NULL;

	/* Going offline */
	e_cal_meta_backend_test_change_online (meta_backend, FALSE);

	e_cal_meta_backend_test_reset_counters (test_backend);

	/* Modify event-2 */
	objects = g_slist_prepend (NULL, ecmb_test_modify_case ("event-2", NULL));

	backend_class->modify_objects_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, objects, E_CAL_OBJ_MOD_ALL, &old_components, &new_components, &error);
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (old_components), ==, 1);
	g_assert_cmpint (g_slist_length (new_components), ==, 1);
	g_assert_cmpint (test_backend->load_count, ==, 0);
	g_assert_cmpint (test_backend->save_count, ==, 0);

	icalcomp = e_cal_component_get_icalcomponent (old_components->data);
	old_sequence = icalcomponent_get_sequence (icalcomp);
	g_assert_cmpstr (icalcomponent_get_summary (icalcomp), !=, MODIFIED_SUMMARY_STR);
	g_assert_cmpstr (icalcomponent_get_uid (icalcomp), ==, "event-2");

	icalcomp = e_cal_component_get_icalcomponent (new_components->data);
	g_assert_cmpint (old_sequence + 1, ==, icalcomponent_get_sequence (icalcomp));
	g_assert_cmpstr (icalcomponent_get_summary (icalcomp), ==, MODIFIED_SUMMARY_STR);
	g_assert_cmpstr (icalcomponent_get_uid (icalcomp), ==, "event-2");

	g_slist_free_full (old_components, g_object_unref);
	g_slist_free_full (new_components, g_object_unref);
	g_slist_free_full (objects, g_free);
	old_components = NULL;
	new_components = NULL;

	/* Going online */
	e_cal_meta_backend_test_change_online (meta_backend, TRUE);

	g_assert_cmpint (test_backend->load_count, ==, 1);
	g_assert_cmpint (test_backend->save_count, ==, 1);

	ecmb_test_cache_and_server_equal (cal_cache, test_backend->vcalendar, E_CACHE_INCLUDE_DELETED);

	offline_changes = e_cal_cache_get_offline_changes (cal_cache, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpint (0, ==, g_slist_length (offline_changes));

	/* Modify non-recurring with THIS */
	objects = g_slist_prepend (NULL, ecmb_test_modify_case ("event-4", NULL));

	backend_class->modify_objects_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, objects, E_CAL_OBJ_MOD_THIS, &old_components, &new_components, &error);
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (old_components), ==, 1);
	g_assert_cmpint (g_slist_length (new_components), ==, 1);
	g_assert_cmpint (test_backend->load_count, ==, 2);
	g_assert_cmpint (test_backend->save_count, ==, 2);

	icalcomp = e_cal_component_get_icalcomponent (old_components->data);
	old_sequence = icalcomponent_get_sequence (icalcomp);
	g_assert_cmpstr (icalcomponent_get_summary (icalcomp), !=, MODIFIED_SUMMARY_STR);
	g_assert_cmpstr (icalcomponent_get_uid (icalcomp), ==, "event-4");

	icalcomp = e_cal_component_get_icalcomponent (new_components->data);
	g_assert_cmpint (old_sequence + 1, ==, icalcomponent_get_sequence (icalcomp));
	g_assert_cmpstr (icalcomponent_get_summary (icalcomp), ==, MODIFIED_SUMMARY_STR);
	g_assert_cmpstr (icalcomponent_get_uid (icalcomp), ==, "event-4");

	g_slist_free_full (old_components, g_object_unref);
	g_slist_free_full (new_components, g_object_unref);
	g_slist_free_full (objects, g_free);
	old_components = NULL;
	new_components = NULL;

	/* Modify non-detached recurring instance with ONLY_THIS */
	objects = g_slist_prepend (NULL, ecmb_test_modify_case ("event-6", "20170227T134900"));

	backend_class->modify_objects_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, objects, E_CAL_OBJ_MOD_ONLY_THIS, &old_components, &new_components, &error);
	g_assert_error (error, E_DATA_CAL_ERROR, ObjectNotFound);
	g_assert_null (old_components);
	g_assert_null (new_components);
	g_assert_cmpint (test_backend->load_count, ==, 2);
	g_assert_cmpint (test_backend->save_count, ==, 2);
	g_clear_error (&error);
	g_slist_free_full (objects, g_free);

	/* Modify detached recurring instance with ONLY_THIS */
	objects = g_slist_prepend (NULL, ecmb_test_modify_case ("event-6-a", NULL));

	backend_class->modify_objects_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, objects, E_CAL_OBJ_MOD_ONLY_THIS, &old_components, &new_components, &error);
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (old_components), ==, 1);
	g_assert_cmpint (g_slist_length (new_components), ==, 1);
	g_assert_cmpint (test_backend->load_count, ==, 3);
	g_assert_cmpint (test_backend->save_count, ==, 3);

	icalcomp = e_cal_component_get_icalcomponent (old_components->data);
	old_sequence = icalcomponent_get_sequence (icalcomp);
	g_assert_cmpstr (icalcomponent_get_uid (icalcomp), ==, "event-6");
	g_assert_cmpstr (icalcomponent_get_summary (icalcomp), !=, MODIFIED_SUMMARY_STR);

	icalcomp = e_cal_component_get_icalcomponent (new_components->data);
	g_assert_cmpstr (icalcomponent_get_uid (icalcomp), ==, "event-6");
	g_assert_cmpstr (icalcomponent_get_summary (icalcomp), ==, MODIFIED_SUMMARY_STR);
	g_assert_cmpint (old_sequence + 1, ==, icalcomponent_get_sequence (icalcomp));

	g_slist_free_full (old_components, g_object_unref);
	g_slist_free_full (new_components, g_object_unref);
	g_slist_free_full (objects, g_free);
	old_components = NULL;
	new_components = NULL;

	ecmb_test_cache_and_server_equal (cal_cache, test_backend->vcalendar, E_CACHE_INCLUDE_DELETED);

	g_object_unref (cal_cache);
}

static void
test_remove_objects (ECalMetaBackend *meta_backend)
{
	ECalMetaBackendTest *test_backend;
	ECalBackendSyncClass *backend_class;
	ECalCache *cal_cache;
	GSList *ids, *old_components = NULL, *new_components = NULL, *offline_changes;
	GError *error = NULL;

	g_assert_nonnull (meta_backend);

	backend_class = E_CAL_BACKEND_SYNC_GET_CLASS (meta_backend);
	g_return_if_fail (backend_class != NULL);
	g_return_if_fail (backend_class->remove_objects_sync != NULL);

	test_backend = E_CAL_META_BACKEND_TEST (meta_backend);
	cal_cache = e_cal_meta_backend_ref_cache (meta_backend);
	g_assert_nonnull (cal_cache);

	/* Remove non-existing event */
	ids = g_slist_prepend (NULL, e_cal_component_id_new ("unknown-event", NULL));

	backend_class->remove_objects_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, ids, E_CAL_OBJ_MOD_ALL, &old_components, &new_components, &error);
	g_assert_error (error, E_DATA_CAL_ERROR, ObjectNotFound);
	g_assert_null (old_components);
	g_assert_null (new_components);
	g_clear_error (&error);
	g_slist_free_full (ids, (GDestroyNotify) e_cal_component_free_id);

	/* Remove existing event */
	ids = g_slist_prepend (NULL, e_cal_component_id_new ("event-1", NULL));

	backend_class->remove_objects_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, ids, E_CAL_OBJ_MOD_ALL, &old_components, &new_components, &error);
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (old_components), ==, 1);
	g_assert_cmpint (g_slist_length (new_components), ==, 1);
	g_assert_null (new_components->data);
	g_assert_cmpint (test_backend->load_count, ==, 0);
	g_assert_cmpint (test_backend->save_count, ==, 0);
	g_assert_cmpint (test_backend->remove_count, ==, 1);

	ecmb_test_vcalendar_contains (test_backend->vcalendar, TRUE, FALSE,
		"event-1", NULL,
		NULL);

	g_slist_free_full (old_components, g_object_unref);
	g_slist_free (new_components);
	g_slist_free_full (ids, (GDestroyNotify) e_cal_component_free_id);
	old_components = NULL;
	new_components = NULL;

	/* Remove existing detached instance */
	ecmb_test_vcalendar_contains (test_backend->vcalendar, FALSE, FALSE,
		"event-6", "20170225T134900",
		NULL);

	ids = g_slist_prepend (NULL, e_cal_component_id_new ("event-6", "20170225T134900"));

	backend_class->remove_objects_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, ids, E_CAL_OBJ_MOD_THIS, &old_components, &new_components, &error);
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (old_components), ==, 1);
	g_assert_cmpint (g_slist_length (new_components), ==, 1);
	g_assert_cmpint (test_backend->load_count, ==, 1);
	g_assert_cmpint (test_backend->save_count, ==, 1);
	g_assert_cmpint (test_backend->remove_count, ==, 1);

	/* Master object is there */
	ecmb_test_vcalendar_contains (test_backend->vcalendar, FALSE, FALSE,
		"event-6", NULL,
		NULL);
	/* Just-removed detached instance is not there */
	ecmb_test_vcalendar_contains (test_backend->vcalendar, TRUE, FALSE,
		"event-6", "20170225T134900",
		NULL);

	g_slist_free_full (old_components, g_object_unref);
	g_slist_free_full (new_components, g_object_unref);
	g_slist_free_full (ids, (GDestroyNotify) e_cal_component_free_id);
	old_components = NULL;
	new_components = NULL;

	/* Remove non-existing detached instance with ONLY_THIS - fails */
	ecmb_test_vcalendar_contains (test_backend->vcalendar, TRUE, FALSE,
		"event-6", "20170227T134900",
		NULL);

	ids = g_slist_prepend (NULL, e_cal_component_id_new ("event-6", "20170227T134900"));

	backend_class->remove_objects_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, ids, E_CAL_OBJ_MOD_ONLY_THIS, &old_components, &new_components, &error);
	g_assert_error (error, E_DATA_CAL_ERROR, ObjectNotFound);
	g_assert_null (old_components);
	g_assert_null (new_components);
	g_clear_error (&error);
	g_slist_free_full (ids, (GDestroyNotify) e_cal_component_free_id);

	/* Remove non-existing detached instance with THIS - changes master object */
	ids = g_slist_prepend (NULL, e_cal_component_id_new ("event-6", "20170227T134900"));

	backend_class->remove_objects_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, ids, E_CAL_OBJ_MOD_THIS, &old_components, &new_components, &error);
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (old_components), ==, 1);
	g_assert_cmpint (g_slist_length (new_components), ==, 1);
	g_assert_cmpint (test_backend->load_count, ==, 2);
	g_assert_cmpint (test_backend->save_count, ==, 2);
	g_assert_cmpint (test_backend->remove_count, ==, 1);

	/* Master object is there */
	ecmb_test_vcalendar_contains (test_backend->vcalendar, FALSE, FALSE,
		"event-6", NULL,
		NULL);
	/* Just-removed detached instance is not there */
	ecmb_test_vcalendar_contains (test_backend->vcalendar, TRUE, FALSE,
		"event-6", "20170227T134900",
		NULL);

	g_slist_free_full (old_components, g_object_unref);
	g_slist_free_full (new_components, g_object_unref);
	g_slist_free_full (ids, (GDestroyNotify) e_cal_component_free_id);
	old_components = NULL;
	new_components = NULL;

	/* Going offline */
	e_cal_meta_backend_test_change_online (meta_backend, FALSE);

	e_cal_meta_backend_test_reset_counters (test_backend);

	/* Remove existing event */
	ids = g_slist_prepend (NULL, e_cal_component_id_new ("event-3", NULL));

	backend_class->remove_objects_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, ids, E_CAL_OBJ_MOD_ONLY_THIS, &old_components, &new_components, &error);
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (old_components), ==, 1);
	g_assert_cmpint (g_slist_length (new_components), ==, 1);
	g_assert_null (new_components->data);
	g_assert_cmpint (test_backend->load_count, ==, 0);
	g_assert_cmpint (test_backend->save_count, ==, 0);
	g_assert_cmpint (test_backend->remove_count, ==, 0);

	ecmb_test_vcalendar_contains (test_backend->vcalendar, FALSE, FALSE,
		"event-3", NULL,
		NULL);
	ecmb_test_cache_contains (cal_cache, TRUE, FALSE,
		"event-3", NULL,
		NULL);

	g_slist_free_full (old_components, g_object_unref);
	g_slist_free (new_components);
	g_slist_free_full (ids, (GDestroyNotify) e_cal_component_free_id);
	old_components = NULL;
	new_components = NULL;

	/* Going online */
	e_cal_meta_backend_test_change_online (meta_backend, TRUE);

	g_assert_cmpint (test_backend->load_count, ==, 0);
	g_assert_cmpint (test_backend->save_count, ==, 0);
	g_assert_cmpint (test_backend->remove_count, ==, 1);

	ecmb_test_vcalendar_contains (test_backend->vcalendar, TRUE, FALSE,
		"event-3", NULL,
		NULL);
	ecmb_test_cache_contains (cal_cache, TRUE, FALSE,
		"event-3", NULL,
		NULL);

	ecmb_test_cache_and_server_equal (cal_cache, test_backend->vcalendar, E_CACHE_INCLUDE_DELETED);

	offline_changes = e_cal_cache_get_offline_changes (cal_cache, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpint (0, ==, g_slist_length (offline_changes));

	g_object_unref (cal_cache);
}

static void
test_receive_objects (ECalMetaBackend *meta_backend)
{
	ECalMetaBackendTest *test_backend;
	ECalBackendSyncClass *backend_class;
	ECalCache *cal_cache;
	gchar *calobj;
	icalcomponent *icalcomp;
	GSList *ids, *old_components = NULL, *new_components = NULL;
	GError *error = NULL;

	g_assert_nonnull (meta_backend);

	backend_class = E_CAL_BACKEND_SYNC_GET_CLASS (meta_backend);
	g_return_if_fail (backend_class != NULL);
	g_return_if_fail (backend_class->get_object_sync != NULL);
	g_return_if_fail (backend_class->receive_objects_sync != NULL);
	g_return_if_fail (backend_class->remove_objects_sync != NULL);

	test_backend = E_CAL_META_BACKEND_TEST (meta_backend);
	cal_cache = e_cal_meta_backend_ref_cache (meta_backend);
	g_assert_nonnull (cal_cache);

	/* Organizer side - receives reply from an attendee */
	calobj = tcu_new_icalstring_from_test_case ("invite-1");
	g_assert_nonnull (calobj);

	icalcomp = icalcomponent_new_from_string (calobj);
	g_assert_nonnull (icalcomp);
	g_assert_nonnull (icalcomponent_get_first_component (icalcomp, ICAL_VEVENT_COMPONENT));
	g_free (calobj);

	icalcomponent_add_component (test_backend->vcalendar, icalcomponent_new_clone (icalcomponent_get_first_component (icalcomp, ICAL_VEVENT_COMPONENT)));

	icalcomponent_free (icalcomp);

	/* To get the 'invite' component into local cache */
	e_cal_meta_backend_test_call_refresh (meta_backend);

	ecmb_test_vcalendar_contains (test_backend->vcalendar, FALSE, FALSE,
		"invite", NULL,
		NULL);
	ecmb_test_cache_contains (cal_cache, FALSE, FALSE,
		"invite", NULL,
		NULL);

	calobj = NULL;
	backend_class->get_object_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, "invite", NULL, &calobj, &error);
	g_assert_no_error (error);
	g_assert_nonnull (calobj);
	g_assert_nonnull (strstr (calobj, "PARTSTAT=NEEDS-ACTION"));
	g_assert_null (strstr (calobj, "PARTSTAT=ACCEPTED"));
	g_free (calobj);

	calobj = tcu_new_icalstring_from_test_case ("invite-2");
	g_assert_nonnull (calobj);

	backend_class->receive_objects_sync (E_CAL_BACKEND_SYNC (meta_backend), NULL, NULL, calobj, &error);
	g_assert_no_error (error);
	g_free (calobj);

	g_assert_cmpint (test_backend->load_count, ==, 2);
	g_assert_cmpint (test_backend->save_count, ==, 1);
	g_assert_cmpint (test_backend->remove_count, ==, 0);

	calobj = NULL;
	backend_class->get_object_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, "invite", NULL, &calobj, &error);
	g_assert_no_error (error);
	g_assert_nonnull (calobj);
	g_assert_null (strstr (calobj, "PARTSTAT=NEEDS-ACTION"));
	g_assert_nonnull (strstr (calobj, "PARTSTAT=ACCEPTED"));
	g_free (calobj);

	ecmb_test_cache_and_server_equal (cal_cache, test_backend->vcalendar, E_CACHE_INCLUDE_DELETED);

	/* Remove the 'invite' component, to test also user side */
	ids = g_slist_prepend (NULL, e_cal_component_id_new ("invite", NULL));

	backend_class->remove_objects_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, ids, E_CAL_OBJ_MOD_ALL, &old_components, &new_components, &error);
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (old_components), ==, 1);
	g_assert_cmpint (g_slist_length (new_components), ==, 1);
	g_assert_null (new_components->data);
	g_assert_cmpint (test_backend->load_count, ==, 2);
	g_assert_cmpint (test_backend->save_count, ==, 1);
	g_assert_cmpint (test_backend->remove_count, ==, 1);

	g_slist_free_full (old_components, g_object_unref);
	g_slist_free (new_components);
	g_slist_free_full (ids, (GDestroyNotify) e_cal_component_free_id);
	old_components = NULL;
	new_components = NULL;

	ecmb_test_vcalendar_contains (test_backend->vcalendar, TRUE, FALSE,
		"invite", NULL,
		NULL);
	ecmb_test_cache_contains (cal_cache, TRUE, FALSE,
		"invite", NULL,
		NULL);

	ecmb_test_cache_and_server_equal (cal_cache, test_backend->vcalendar, E_CACHE_INCLUDE_DELETED);

	/* User side - receives invitation */
	calobj = tcu_new_icalstring_from_test_case ("invite-1");
	g_assert_nonnull (calobj);

	backend_class->receive_objects_sync (E_CAL_BACKEND_SYNC (meta_backend), NULL, NULL, calobj, &error);
	g_assert_no_error (error);
	g_free (calobj);

	g_assert_cmpint (test_backend->load_count, ==, 3);
	g_assert_cmpint (test_backend->save_count, ==, 2);
	g_assert_cmpint (test_backend->remove_count, ==, 1);

	ecmb_test_vcalendar_contains (test_backend->vcalendar, FALSE, FALSE,
		"invite", NULL,
		NULL);
	ecmb_test_cache_contains (cal_cache, FALSE, FALSE,
		"invite", NULL,
		NULL);

	ecmb_test_cache_and_server_equal (cal_cache, test_backend->vcalendar, E_CACHE_INCLUDE_DELETED);

	calobj = NULL;
	backend_class->get_object_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, "invite", NULL, &calobj, &error);
	g_assert_no_error (error);
	g_assert_nonnull (calobj);
	g_assert_nonnull (strstr (calobj, "SUMMARY:Invite\r\n"));
	g_assert_null (strstr (calobj, "SUMMARY:Invite (modified)\r\n"));
	g_free (calobj);

	/* Receives update from organizer */
	calobj = tcu_new_icalstring_from_test_case ("invite-3");
	g_assert_nonnull (calobj);

	backend_class->receive_objects_sync (E_CAL_BACKEND_SYNC (meta_backend), NULL, NULL, calobj, &error);
	g_assert_no_error (error);
	g_free (calobj);

	g_assert_cmpint (test_backend->load_count, ==, 4);
	g_assert_cmpint (test_backend->save_count, ==, 3);
	g_assert_cmpint (test_backend->remove_count, ==, 1);

	ecmb_test_vcalendar_contains (test_backend->vcalendar, FALSE, FALSE,
		"invite", NULL,
		NULL);
	ecmb_test_cache_contains (cal_cache, FALSE, FALSE,
		"invite", NULL,
		NULL);

	ecmb_test_cache_and_server_equal (cal_cache, test_backend->vcalendar, E_CACHE_INCLUDE_DELETED);

	calobj = NULL;
	backend_class->get_object_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, "invite", NULL, &calobj, &error);
	g_assert_no_error (error);
	g_assert_nonnull (calobj);
	g_assert_null (strstr (calobj, "SUMMARY:Invite\r\n"));
	g_assert_nonnull (strstr (calobj, "SUMMARY:Invite (modified)\r\n"));
	g_free (calobj);

	/* Receives cancellation from organizer */
	calobj = tcu_new_icalstring_from_test_case ("invite-4");
	g_assert_nonnull (calobj);

	backend_class->receive_objects_sync (E_CAL_BACKEND_SYNC (meta_backend), NULL, NULL, calobj, &error);
	g_assert_no_error (error);
	g_free (calobj);

	g_assert_cmpint (test_backend->load_count, ==, 4);
	g_assert_cmpint (test_backend->save_count, ==, 3);
	g_assert_cmpint (test_backend->remove_count, ==, 2);

	ecmb_test_vcalendar_contains (test_backend->vcalendar, TRUE, FALSE,
		"invite", NULL,
		NULL);
	ecmb_test_cache_contains (cal_cache, TRUE, FALSE,
		"invite", NULL,
		NULL);

	ecmb_test_cache_and_server_equal (cal_cache, test_backend->vcalendar, E_CACHE_INCLUDE_DELETED);

	g_object_unref (cal_cache);
}

static void
test_receive_and_remove (ECalMetaBackend *meta_backend)
{
	ECalMetaBackendTest *test_backend;
	ECalBackendSyncClass *backend_class;
	ECalCache *cal_cache;
	gchar *calobj;
	GSList *old_components = NULL, *new_components = NULL, *ids;
	GError *error = NULL;

	g_assert_nonnull (meta_backend);

	backend_class = E_CAL_BACKEND_SYNC_GET_CLASS (meta_backend);
	g_return_if_fail (backend_class != NULL);
	g_return_if_fail (backend_class->get_object_sync != NULL);
	g_return_if_fail (backend_class->remove_objects_sync != NULL);
	g_return_if_fail (backend_class->receive_objects_sync != NULL);

	test_backend = E_CAL_META_BACKEND_TEST (meta_backend);
	cal_cache = e_cal_meta_backend_ref_cache (meta_backend);
	g_assert_nonnull (cal_cache);

	/* Receive master component */
	calobj = tcu_new_icalstring_from_test_case ("invite-5");
	g_assert_nonnull (calobj);

	backend_class->receive_objects_sync (E_CAL_BACKEND_SYNC (meta_backend), NULL, NULL, calobj, &error);
	g_assert_no_error (error);
	g_free (calobj);

	g_assert_cmpint (test_backend->load_count, ==, 1);
	g_assert_cmpint (test_backend->save_count, ==, 1);
	g_assert_cmpint (test_backend->remove_count, ==, 0);

	ecmb_test_vcalendar_contains (test_backend->vcalendar, FALSE, FALSE,
		"invite-detached", NULL,
		NULL);
	ecmb_test_cache_contains (cal_cache, FALSE, FALSE,
		"invite-detached", NULL,
		NULL);

	ecmb_test_cache_and_server_equal (cal_cache, test_backend->vcalendar, E_CACHE_INCLUDE_DELETED);

	calobj = NULL;
	backend_class->get_object_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, "invite-detached", NULL, &calobj, &error);
	g_assert_no_error (error);
	g_assert_nonnull (calobj);
	g_assert_nonnull (strstr (calobj, "SUMMARY:Recurring invite\r\n"));
	g_assert_null (strstr (calobj, "SUMMARY:Detached instance of recurring invite\r\n"));
	g_free (calobj);

	/* Delete master component, with no detached instances now */
	ids = g_slist_prepend (NULL, e_cal_component_id_new ("invite-detached", NULL));

	backend_class->remove_objects_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, ids, E_CAL_OBJ_MOD_ALL, &old_components, &new_components, &error);
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (old_components), ==, 1);
	g_assert_cmpint (g_slist_length (new_components), ==, 1);
	g_assert_null (new_components->data);
	g_assert_cmpint (test_backend->load_count, ==, 1);
	g_assert_cmpint (test_backend->save_count, ==, 1);
	g_assert_cmpint (test_backend->remove_count, ==, 1);

	g_slist_free_full (old_components, g_object_unref);
	g_slist_free (new_components);
	g_slist_free_full (ids, (GDestroyNotify) e_cal_component_free_id);
	old_components = NULL;
	new_components = NULL;

	ecmb_test_vcalendar_contains (test_backend->vcalendar, TRUE, FALSE,
		"invite-detached", NULL,
		NULL);
	ecmb_test_cache_contains (cal_cache, TRUE, FALSE,
		"invite-detached", NULL,
		NULL);

	ecmb_test_cache_and_server_equal (cal_cache, test_backend->vcalendar, E_CACHE_INCLUDE_DELETED);

	/* Receive the master component again */
	calobj = tcu_new_icalstring_from_test_case ("invite-5");
	g_assert_nonnull (calobj);

	backend_class->receive_objects_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, calobj, &error);
	g_assert_no_error (error);
	g_free (calobj);

	g_assert_cmpint (test_backend->load_count, ==, 2);
	g_assert_cmpint (test_backend->save_count, ==, 2);
	g_assert_cmpint (test_backend->remove_count, ==, 1);

	ecmb_test_vcalendar_contains (test_backend->vcalendar, FALSE, FALSE,
		"invite-detached", NULL,
		NULL);
	ecmb_test_cache_contains (cal_cache, FALSE, FALSE,
		"invite-detached", NULL,
		NULL);

	ecmb_test_cache_and_server_equal (cal_cache, test_backend->vcalendar, E_CACHE_INCLUDE_DELETED);

	/* Then receive the detached instance */
	calobj = tcu_new_icalstring_from_test_case ("invite-6");
	g_assert_nonnull (calobj);

	backend_class->receive_objects_sync (E_CAL_BACKEND_SYNC (meta_backend), NULL, NULL, calobj, &error);
	g_assert_no_error (error);
	g_free (calobj);

	g_assert_cmpint (test_backend->load_count, ==, 3);
	g_assert_cmpint (test_backend->save_count, ==, 3);
	g_assert_cmpint (test_backend->remove_count, ==, 1);

	ecmb_test_vcalendar_contains (test_backend->vcalendar, FALSE, FALSE,
		"invite-detached", NULL,
		"invite-detached", "20180502T000000Z",
		NULL);
	ecmb_test_cache_contains (cal_cache, FALSE, FALSE,
		"invite-detached", NULL,
		"invite-detached", "20180502T000000Z",
		NULL);

	ecmb_test_cache_and_server_equal (cal_cache, test_backend->vcalendar, E_CACHE_INCLUDE_DELETED);

	calobj = NULL;
	backend_class->get_object_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, "invite-detached", NULL, &calobj, &error);
	g_assert_no_error (error);
	g_assert_nonnull (calobj);
	g_assert_nonnull (strstr (calobj, "SUMMARY:Recurring invite\r\n"));
	g_assert_nonnull (strstr (calobj, "SUMMARY:Detached instance of recurring invite\r\n"));
	g_free (calobj);

	/* Remove only the detached instance */
	ids = g_slist_prepend (NULL, e_cal_component_id_new ("invite-detached", "20180502T000000Z"));

	backend_class->remove_objects_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, ids, E_CAL_OBJ_MOD_THIS, &old_components, &new_components, &error);
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (old_components), ==, 1);
	g_assert_cmpint (g_slist_length (new_components), ==, 1);
	g_assert_nonnull (new_components->data);
	g_assert_cmpint (test_backend->load_count, ==, 4);
	g_assert_cmpint (test_backend->save_count, ==, 4);
	g_assert_cmpint (test_backend->remove_count, ==, 1);

	g_slist_free_full (old_components, g_object_unref);
	g_slist_free_full (new_components, g_object_unref);
	g_slist_free_full (ids, (GDestroyNotify) e_cal_component_free_id);
	old_components = NULL;
	new_components = NULL;

	ecmb_test_vcalendar_contains (test_backend->vcalendar, FALSE, FALSE,
		"invite-detached", NULL,
		NULL);
	ecmb_test_cache_contains (cal_cache, FALSE, FALSE,
		"invite-detached", NULL,
		NULL);
	ecmb_test_vcalendar_contains (test_backend->vcalendar, TRUE, FALSE,
		"invite-detached", "20180502T000000Z",
		NULL);
	ecmb_test_cache_contains (cal_cache, TRUE, FALSE,
		"invite-detached", "20180502T000000Z",
		NULL);

	ecmb_test_cache_and_server_equal (cal_cache, test_backend->vcalendar, E_CACHE_INCLUDE_DELETED);

	calobj = NULL;
	backend_class->get_object_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, "invite-detached", NULL, &calobj, &error);
	g_assert_no_error (error);
	g_assert_nonnull (calobj);
	g_assert_nonnull (strstr (calobj, "SUMMARY:Recurring invite\r\n"));
	g_assert_null (strstr (calobj, "SUMMARY:Detached instance of recurring invite\r\n"));
	g_free (calobj);

	/* Receive the detached instance again */
	calobj = tcu_new_icalstring_from_test_case ("invite-6");
	g_assert_nonnull (calobj);

	backend_class->receive_objects_sync (E_CAL_BACKEND_SYNC (meta_backend), NULL, NULL, calobj, &error);
	g_assert_no_error (error);
	g_free (calobj);

	g_assert_cmpint (test_backend->load_count, ==, 5);
	g_assert_cmpint (test_backend->save_count, ==, 5);
	g_assert_cmpint (test_backend->remove_count, ==, 1);

	ecmb_test_vcalendar_contains (test_backend->vcalendar, FALSE, FALSE,
		"invite-detached", NULL,
		NULL);
	ecmb_test_cache_contains (cal_cache, FALSE, FALSE,
		"invite-detached", NULL,
		NULL);

	ecmb_test_cache_and_server_equal (cal_cache, test_backend->vcalendar, E_CACHE_INCLUDE_DELETED);

	calobj = NULL;
	backend_class->get_object_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, "invite-detached", NULL, &calobj, &error);
	g_assert_no_error (error);
	g_assert_nonnull (calobj);
	g_assert_nonnull (strstr (calobj, "SUMMARY:Recurring invite\r\n"));
	g_assert_nonnull (strstr (calobj, "SUMMARY:Detached instance of recurring invite\r\n"));
	g_free (calobj);

	/* Remove the master object, which should delete both */
	ids = g_slist_prepend (NULL, e_cal_component_id_new ("invite-detached", NULL));

	backend_class->remove_objects_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, ids, E_CAL_OBJ_MOD_ALL, &old_components, &new_components, &error);
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (old_components), ==, 1);
	g_assert_cmpint (g_slist_length (new_components), ==, 1);
	g_assert_null (new_components->data);
	g_assert_cmpint (test_backend->load_count, ==, 5);
	g_assert_cmpint (test_backend->save_count, ==, 5);
	g_assert_cmpint (test_backend->remove_count, ==, 2);

	g_slist_free_full (old_components, g_object_unref);
	g_slist_free (new_components);
	g_slist_free_full (ids, (GDestroyNotify) e_cal_component_free_id);
	old_components = NULL;
	new_components = NULL;

	ecmb_test_vcalendar_contains (test_backend->vcalendar, TRUE, FALSE,
		"invite-detached", NULL,
		NULL);
	ecmb_test_cache_contains (cal_cache, TRUE, FALSE,
		"invite-detached", NULL,
		NULL);

	ecmb_test_cache_and_server_equal (cal_cache, test_backend->vcalendar, E_CACHE_INCLUDE_DELETED);

	calobj = NULL;
	backend_class->get_object_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, "invite-detached", NULL, &calobj, &error);
	g_assert_error (error, E_DATA_CAL_ERROR, ObjectNotFound);
	g_assert_null (calobj);
	g_clear_error (&error);

	backend_class->get_object_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, "invite-detached", "20180502T000000Z", &calobj, &error);
	g_assert_error (error, E_DATA_CAL_ERROR, ObjectNotFound);
	g_assert_null (calobj);
	g_clear_error (&error);

	/* Receive only the detached instance, with no master object in the cache */
	calobj = tcu_new_icalstring_from_test_case ("invite-6");
	g_assert_nonnull (calobj);

	backend_class->receive_objects_sync (E_CAL_BACKEND_SYNC (meta_backend), NULL, NULL, calobj, &error);
	g_assert_no_error (error);
	g_free (calobj);

	g_assert_cmpint (test_backend->load_count, ==, 8);
	g_assert_cmpint (test_backend->save_count, ==, 6);
	g_assert_cmpint (test_backend->remove_count, ==, 2);

	ecmb_test_vcalendar_contains (test_backend->vcalendar, FALSE, FALSE,
		"invite-detached", "20180502T000000Z",
		NULL);
	ecmb_test_cache_contains (cal_cache, FALSE, FALSE,
		"invite-detached", "20180502T000000Z",
		NULL);

	/* Remove the master object with mode THIS, which is not in the cache, but should remove all anyway */
	ids = g_slist_prepend (NULL, e_cal_component_id_new ("invite-detached", NULL));

	backend_class->remove_objects_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, ids, E_CAL_OBJ_MOD_THIS, &old_components, &new_components, &error);
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (old_components), ==, 1);
	g_assert_cmpint (g_slist_length (new_components), ==, 1);
	g_assert_null (new_components->data);
	g_assert_cmpint (test_backend->load_count, ==, 8);
	g_assert_cmpint (test_backend->save_count, ==, 6);
	g_assert_cmpint (test_backend->remove_count, ==, 3);

	g_slist_free_full (old_components, g_object_unref);
	g_slist_free (new_components);
	g_slist_free_full (ids, (GDestroyNotify) e_cal_component_free_id);
	old_components = NULL;
	new_components = NULL;

	ecmb_test_vcalendar_contains (test_backend->vcalendar, TRUE, FALSE,
		"invite-detached", "20180502T000000Z",
		NULL);
	ecmb_test_cache_contains (cal_cache, TRUE, FALSE,
		"invite-detached", "20180502T000000Z",
		NULL);

	ecmb_test_cache_and_server_equal (cal_cache, test_backend->vcalendar, E_CACHE_INCLUDE_DELETED);

	calobj = tcu_new_icalstring_from_test_case ("invite-6");
	g_assert_nonnull (calobj);

	backend_class->receive_objects_sync (E_CAL_BACKEND_SYNC (meta_backend), NULL, NULL, calobj, &error);
	g_assert_no_error (error);
	g_free (calobj);

	/* Remove the master object with mode ALL, which is not in the cache, but should remove all */
	ids = g_slist_prepend (NULL, e_cal_component_id_new ("invite-detached", NULL));

	backend_class->remove_objects_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, ids, E_CAL_OBJ_MOD_ALL, &old_components, &new_components, &error);
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (old_components), ==, 1);
	g_assert_cmpint (g_slist_length (new_components), ==, 1);
	g_assert_null (new_components->data);
	g_assert_cmpint (test_backend->load_count, ==, 9);
	g_assert_cmpint (test_backend->save_count, ==, 7);
	g_assert_cmpint (test_backend->remove_count, ==, 4);

	g_slist_free_full (old_components, g_object_unref);
	g_slist_free (new_components);
	g_slist_free_full (ids, (GDestroyNotify) e_cal_component_free_id);
	old_components = NULL;
	new_components = NULL;

	ecmb_test_vcalendar_contains (test_backend->vcalendar, TRUE, FALSE,
		"invite-detached", NULL,
		"invite-detached", "20180502T000000Z",
		NULL);
	ecmb_test_cache_contains (cal_cache, TRUE, FALSE,
		"invite-detached", NULL,
		"invite-detached", "20180502T000000Z",
		NULL);

	ecmb_test_cache_and_server_equal (cal_cache, test_backend->vcalendar, E_CACHE_INCLUDE_DELETED);

	/* Receive only the detached instance, with no master object in the cache */
	calobj = tcu_new_icalstring_from_test_case ("invite-6");
	g_assert_nonnull (calobj);

	backend_class->receive_objects_sync (E_CAL_BACKEND_SYNC (meta_backend), NULL, NULL, calobj, &error);
	g_assert_no_error (error);
	g_free (calobj);

	g_assert_cmpint (test_backend->load_count, ==, 10);
	g_assert_cmpint (test_backend->save_count, ==, 8);
	g_assert_cmpint (test_backend->remove_count, ==, 4);

	ecmb_test_vcalendar_contains (test_backend->vcalendar, FALSE, FALSE,
		"invite-detached", "20180502T000000Z",
		NULL);
	ecmb_test_cache_contains (cal_cache, FALSE, FALSE,
		"invite-detached", "20180502T000000Z",
		NULL);

	/* Remove the detached instance with mode THIS */
	ids = g_slist_prepend (NULL, e_cal_component_id_new ("invite-detached", "20180502T000000Z"));

	backend_class->remove_objects_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, ids, E_CAL_OBJ_MOD_THIS, &old_components, &new_components, &error);
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (old_components), ==, 1);
	g_assert_cmpint (g_slist_length (new_components), ==, 1);
	g_assert_null (new_components->data);
	g_assert_cmpint (test_backend->load_count, ==, 10);
	g_assert_cmpint (test_backend->save_count, ==, 8);
	g_assert_cmpint (test_backend->remove_count, ==, 5);

	g_slist_free_full (old_components, g_object_unref);
	g_slist_free (new_components);
	g_slist_free_full (ids, (GDestroyNotify) e_cal_component_free_id);
	old_components = NULL;
	new_components = NULL;

	ecmb_test_vcalendar_contains (test_backend->vcalendar, TRUE, FALSE,
		"invite-detached", "20180502T000000Z",
		NULL);
	ecmb_test_cache_contains (cal_cache, TRUE, FALSE,
		"invite-detached", "20180502T000000Z",
		NULL);

	ecmb_test_cache_and_server_equal (cal_cache, test_backend->vcalendar, E_CACHE_INCLUDE_DELETED);

	/* Receive only the detached instance, with no master object in the cache */
	calobj = tcu_new_icalstring_from_test_case ("invite-6");
	g_assert_nonnull (calobj);

	backend_class->receive_objects_sync (E_CAL_BACKEND_SYNC (meta_backend), NULL, NULL, calobj, &error);
	g_assert_no_error (error);
	g_free (calobj);

	g_assert_cmpint (test_backend->load_count, ==, 11);
	g_assert_cmpint (test_backend->save_count, ==, 9);
	g_assert_cmpint (test_backend->remove_count, ==, 5);

	ecmb_test_vcalendar_contains (test_backend->vcalendar, FALSE, FALSE,
		"invite-detached", "20180502T000000Z",
		NULL);
	ecmb_test_cache_contains (cal_cache, FALSE, FALSE,
		"invite-detached", "20180502T000000Z",
		NULL);

	/* Remove the detached instance with mode ALL */
	ids = g_slist_prepend (NULL, e_cal_component_id_new ("invite-detached", "20180502T000000Z"));

	backend_class->remove_objects_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, ids, E_CAL_OBJ_MOD_ALL, &old_components, &new_components, &error);
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (old_components), ==, 1);
	g_assert_cmpint (g_slist_length (new_components), ==, 1);
	g_assert_null (new_components->data);
	g_assert_cmpint (test_backend->load_count, ==, 11);
	g_assert_cmpint (test_backend->save_count, ==, 9);
	g_assert_cmpint (test_backend->remove_count, ==, 6);

	g_slist_free_full (old_components, g_object_unref);
	g_slist_free (new_components);
	g_slist_free_full (ids, (GDestroyNotify) e_cal_component_free_id);
	old_components = NULL;
	new_components = NULL;

	ecmb_test_vcalendar_contains (test_backend->vcalendar, TRUE, FALSE,
		"invite-detached", "20180502T000000Z",
		NULL);
	ecmb_test_cache_contains (cal_cache, TRUE, FALSE,
		"invite-detached", "20180502T000000Z",
		NULL);

	ecmb_test_cache_and_server_equal (cal_cache, test_backend->vcalendar, E_CACHE_INCLUDE_DELETED);

	/* Receive only two detached instances, with no master object in the cache */
	calobj = tcu_new_icalstring_from_test_case ("invite-7");
	g_assert_nonnull (calobj);

	backend_class->receive_objects_sync (E_CAL_BACKEND_SYNC (meta_backend), NULL, NULL, calobj, &error);
	g_assert_no_error (error);
	g_free (calobj);

	g_assert_cmpint (test_backend->load_count, ==, 13);
	g_assert_cmpint (test_backend->save_count, ==, 11);
	g_assert_cmpint (test_backend->remove_count, ==, 6);

	ecmb_test_vcalendar_contains (test_backend->vcalendar, FALSE, FALSE,
		"invite-detached", "20180502T000000Z",
		"invite-detached", "20180509T000000Z",
		NULL);
	ecmb_test_cache_contains (cal_cache, FALSE, FALSE,
		"invite-detached", "20180502T000000Z",
		"invite-detached", "20180509T000000Z",
		NULL);

	/* Remove the detached instance with mode THIS */
	ids = g_slist_prepend (NULL, e_cal_component_id_new ("invite-detached", "20180502T000000Z"));

	backend_class->remove_objects_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, ids, E_CAL_OBJ_MOD_THIS, &old_components, &new_components, &error);
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (old_components), ==, 1);
	g_assert_cmpint (g_slist_length (new_components), ==, 1);
	g_assert_null (new_components->data);
	g_assert_cmpint (test_backend->load_count, ==, 14);
	g_assert_cmpint (test_backend->save_count, ==, 12);
	g_assert_cmpint (test_backend->remove_count, ==, 6);

	g_slist_free_full (old_components, g_object_unref);
	g_slist_free (new_components);
	g_slist_free_full (ids, (GDestroyNotify) e_cal_component_free_id);
	old_components = NULL;
	new_components = NULL;

	ecmb_test_vcalendar_contains (test_backend->vcalendar, TRUE, FALSE,
		"invite-detached", "20180502T000000Z",
		NULL);
	ecmb_test_cache_contains (cal_cache, TRUE, FALSE,
		"invite-detached", "20180502T000000Z",
		NULL);
	ecmb_test_vcalendar_contains (test_backend->vcalendar, FALSE, FALSE,
		"invite-detached", "20180509T000000Z",
		NULL);
	ecmb_test_cache_contains (cal_cache, FALSE, FALSE,
		"invite-detached", "20180509T000000Z",
		NULL);

	ecmb_test_cache_and_server_equal (cal_cache, test_backend->vcalendar, E_CACHE_INCLUDE_DELETED);

	/* Receive the removed component again */
	calobj = tcu_new_icalstring_from_test_case ("invite-6");
	g_assert_nonnull (calobj);

	backend_class->receive_objects_sync (E_CAL_BACKEND_SYNC (meta_backend), NULL, NULL, calobj, &error);
	g_assert_no_error (error);
	g_free (calobj);

	/* Remove both detached instances with mode THIS */
	ids = g_slist_prepend (NULL, e_cal_component_id_new ("invite-detached", "20180502T000000Z"));
	ids = g_slist_prepend (ids, e_cal_component_id_new ("invite-detached", "20180509T000000Z"));

	backend_class->remove_objects_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, ids, E_CAL_OBJ_MOD_THIS, &old_components, &new_components, &error);
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (old_components), ==, 2);
	g_assert_cmpint (g_slist_length (new_components), ==, 2);
	g_assert_null (new_components->data);
	g_assert_null (new_components->next->data);
	g_assert_cmpint (test_backend->load_count, ==, 16);
	g_assert_cmpint (test_backend->save_count, ==, 14);
	g_assert_cmpint (test_backend->remove_count, ==, 7);

	g_slist_free_full (old_components, g_object_unref);
	g_slist_free (new_components);
	g_slist_free_full (ids, (GDestroyNotify) e_cal_component_free_id);
	old_components = NULL;
	new_components = NULL;

	ecmb_test_vcalendar_contains (test_backend->vcalendar, TRUE, FALSE,
		"invite-detached", "20180502T000000Z",
		"invite-detached", "20180509T000000Z",
		NULL);
	ecmb_test_cache_contains (cal_cache, TRUE, FALSE,
		"invite-detached", "20180502T000000Z",
		"invite-detached", "20180509T000000Z",
		NULL);

	ecmb_test_cache_and_server_equal (cal_cache, test_backend->vcalendar, E_CACHE_INCLUDE_DELETED);

	/* Receive only two detached instances, with no master object in the cache */
	calobj = tcu_new_icalstring_from_test_case ("invite-7");
	g_assert_nonnull (calobj);

	backend_class->receive_objects_sync (E_CAL_BACKEND_SYNC (meta_backend), NULL, NULL, calobj, &error);
	g_assert_no_error (error);
	g_free (calobj);

	g_assert_cmpint (test_backend->load_count, ==, 18);
	g_assert_cmpint (test_backend->save_count, ==, 16);
	g_assert_cmpint (test_backend->remove_count, ==, 7);

	ecmb_test_vcalendar_contains (test_backend->vcalendar, FALSE, FALSE,
		"invite-detached", "20180502T000000Z",
		"invite-detached", "20180509T000000Z",
		NULL);
	ecmb_test_cache_contains (cal_cache, FALSE, FALSE,
		"invite-detached", "20180502T000000Z",
		"invite-detached", "20180509T000000Z",
		NULL);

	/* Remove the second detached instance with mode ALL */
	ids = g_slist_prepend (NULL, e_cal_component_id_new ("invite-detached", "20180509T000000Z"));

	backend_class->remove_objects_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, ids, E_CAL_OBJ_MOD_ALL, &old_components, &new_components, &error);
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (old_components), ==, 1);
	g_assert_cmpint (g_slist_length (new_components), ==, 1);
	g_assert_null (new_components->data);
	g_assert_cmpint (test_backend->load_count, ==, 18);
	g_assert_cmpint (test_backend->save_count, ==, 16);
	g_assert_cmpint (test_backend->remove_count, ==, 8);

	g_slist_free_full (old_components, g_object_unref);
	g_slist_free (new_components);
	g_slist_free_full (ids, (GDestroyNotify) e_cal_component_free_id);
	old_components = NULL;
	new_components = NULL;

	ecmb_test_vcalendar_contains (test_backend->vcalendar, TRUE, FALSE,
		"invite-detached", "20180502T000000Z",
		"invite-detached", "20180509T000000Z",
		NULL);
	ecmb_test_cache_contains (cal_cache, TRUE, FALSE,
		"invite-detached", "20180502T000000Z",
		"invite-detached", "20180509T000000Z",
		NULL);

	ecmb_test_cache_and_server_equal (cal_cache, test_backend->vcalendar, E_CACHE_INCLUDE_DELETED);

	/* Receive only two detached instances, with no master object in the cache */
	calobj = tcu_new_icalstring_from_test_case ("invite-7");
	g_assert_nonnull (calobj);

	backend_class->receive_objects_sync (E_CAL_BACKEND_SYNC (meta_backend), NULL, NULL, calobj, &error);
	g_assert_no_error (error);
	g_free (calobj);

	g_assert_cmpint (test_backend->load_count, ==, 20);
	g_assert_cmpint (test_backend->save_count, ==, 18);
	g_assert_cmpint (test_backend->remove_count, ==, 8);

	ecmb_test_vcalendar_contains (test_backend->vcalendar, FALSE, FALSE,
		"invite-detached", "20180502T000000Z",
		"invite-detached", "20180509T000000Z",
		NULL);
	ecmb_test_cache_contains (cal_cache, FALSE, FALSE,
		"invite-detached", "20180502T000000Z",
		"invite-detached", "20180509T000000Z",
		NULL);
	ecmb_test_vcalendar_contains (test_backend->vcalendar, TRUE, FALSE,
		"invite-detached", NULL,
		NULL);
	ecmb_test_cache_contains (cal_cache, TRUE, FALSE,
		"invite-detached", NULL,
		NULL);

	ecmb_test_cache_and_server_equal (cal_cache, test_backend->vcalendar, E_CACHE_INCLUDE_DELETED);

	/* Receive the master object */
	calobj = tcu_new_icalstring_from_test_case ("invite-5");
	g_assert_nonnull (calobj);

	backend_class->receive_objects_sync (E_CAL_BACKEND_SYNC (meta_backend), NULL, NULL, calobj, &error);
	g_assert_no_error (error);
	g_free (calobj);

	g_assert_cmpint (test_backend->load_count, ==, 21);
	g_assert_cmpint (test_backend->save_count, ==, 19);
	g_assert_cmpint (test_backend->remove_count, ==, 8);

	ecmb_test_vcalendar_contains (test_backend->vcalendar, FALSE, FALSE,
		"invite-detached", NULL,
		"invite-detached", "20180502T000000Z",
		"invite-detached", "20180509T000000Z",
		NULL);
	ecmb_test_cache_contains (cal_cache, FALSE, FALSE,
		"invite-detached", NULL,
		"invite-detached", "20180502T000000Z",
		"invite-detached", "20180509T000000Z",
		NULL);

	ecmb_test_cache_and_server_equal (cal_cache, test_backend->vcalendar, E_CACHE_INCLUDE_DELETED);

	/* Remove the second detached instance with mode THIS */
	ids = g_slist_prepend (NULL, e_cal_component_id_new ("invite-detached", "20180509T000000Z"));

	backend_class->remove_objects_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, ids, E_CAL_OBJ_MOD_THIS, &old_components, &new_components, &error);
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (old_components), ==, 1);
	g_assert_cmpint (g_slist_length (new_components), ==, 1);
	g_assert_nonnull (new_components->data);
	g_assert_cmpint (test_backend->load_count, ==, 22);
	g_assert_cmpint (test_backend->save_count, ==, 20);
	g_assert_cmpint (test_backend->remove_count, ==, 8);

	g_slist_free_full (old_components, g_object_unref);
	g_slist_free_full (new_components, g_object_unref);
	g_slist_free_full (ids, (GDestroyNotify) e_cal_component_free_id);
	old_components = NULL;
	new_components = NULL;

	ecmb_test_vcalendar_contains (test_backend->vcalendar, TRUE, FALSE,
		"invite-detached", "20180509T000000Z",
		NULL);
	ecmb_test_cache_contains (cal_cache, TRUE, FALSE,
		"invite-detached", "20180509T000000Z",
		NULL);
	ecmb_test_vcalendar_contains (test_backend->vcalendar, FALSE, FALSE,
		"invite-detached", NULL,
		"invite-detached", "20180502T000000Z",
		NULL);
	ecmb_test_cache_contains (cal_cache, FALSE, FALSE,
		"invite-detached", NULL,
		"invite-detached", "20180502T000000Z",
		NULL);

	ecmb_test_cache_and_server_equal (cal_cache, test_backend->vcalendar, E_CACHE_INCLUDE_DELETED);

	/* Remove the first detached instance with mode ALL, which will drop whole series */
	ids = g_slist_prepend (NULL, e_cal_component_id_new ("invite-detached", "20180502T000000Z"));

	backend_class->remove_objects_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, ids, E_CAL_OBJ_MOD_ALL, &old_components, &new_components, &error);
	g_assert_no_error (error);
	g_assert_cmpint (g_slist_length (old_components), ==, 1);
	g_assert_cmpint (g_slist_length (new_components), ==, 1);
	g_assert_null (new_components->data);
	g_assert_cmpint (test_backend->load_count, ==, 22);
	g_assert_cmpint (test_backend->save_count, ==, 20);
	g_assert_cmpint (test_backend->remove_count, ==, 9);

	g_slist_free_full (old_components, g_object_unref);
	g_slist_free (new_components);
	g_slist_free_full (ids, (GDestroyNotify) e_cal_component_free_id);
	old_components = NULL;
	new_components = NULL;

	ecmb_test_vcalendar_contains (test_backend->vcalendar, TRUE, FALSE,
		"invite-detached", NULL,
		"invite-detached", "20180502T000000Z",
		"invite-detached", "20180509T000000Z",
		NULL);
	ecmb_test_cache_contains (cal_cache, TRUE, FALSE,
		"invite-detached", NULL,
		"invite-detached", "20180502T000000Z",
		"invite-detached", "20180509T000000Z",
		NULL);

	ecmb_test_cache_and_server_equal (cal_cache, test_backend->vcalendar, E_CACHE_INCLUDE_DELETED);

	g_object_unref (cal_cache);
}

static void
test_get_object (ECalMetaBackend *meta_backend)
{
	ECalMetaBackendTest *test_backend;
	ECalBackendSyncClass *backend_class;
	ECalCache *cal_cache;
	icalcomponent *icalcomp;
	gchar *calobj = NULL;
	GError *error = NULL;

	g_assert_nonnull (meta_backend);

	backend_class = E_CAL_BACKEND_SYNC_GET_CLASS (meta_backend);
	g_return_if_fail (backend_class != NULL);
	g_return_if_fail (backend_class->get_object_sync != NULL);

	test_backend = E_CAL_META_BACKEND_TEST (meta_backend);
	cal_cache = e_cal_meta_backend_ref_cache (meta_backend);
	g_assert_nonnull (cal_cache);

	e_cal_cache_remove_component (cal_cache, "event-7", NULL, E_CACHE_IS_ONLINE, NULL, &error);
	g_assert_no_error (error);
	e_cal_cache_remove_component (cal_cache, "event-8", NULL, E_CACHE_IS_ONLINE, NULL, &error);
	g_assert_no_error (error);
	e_cal_cache_remove_component (cal_cache, "event-9", NULL, E_CACHE_IS_ONLINE, NULL, &error);
	g_assert_no_error (error);

	/* Master object with its detached instances */
	backend_class->get_object_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, "event-6", NULL, &calobj, &error);

	g_assert_no_error (error);
	g_assert_nonnull (calobj);
	g_assert (strstr (calobj, "UID:event-6"));
	g_assert (strstr (calobj, "RECURRENCE-ID;TZID=America/New_York:20170225T134900"));

	icalcomp = icalcomponent_new_from_string (calobj);
	g_assert_nonnull (icalcomp);
	g_assert_cmpint (icalcomponent_isa (icalcomp), ==, ICAL_VCALENDAR_COMPONENT);
	g_assert_cmpint (icalcomponent_count_components (icalcomp, ICAL_VEVENT_COMPONENT), ==, 2);
	icalcomponent_free (icalcomp);

	g_free (calobj);
	calobj = NULL;

	/* Only the detached instance */
	backend_class->get_object_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, "event-6", "20170225T134900", &calobj, &error);

	g_assert_no_error (error);
	g_assert_nonnull (calobj);
	g_assert (strstr (calobj, "UID:event-6"));
	g_assert (strstr (calobj, "RECURRENCE-ID;TZID=America/New_York:20170225T134900"));

	icalcomp = icalcomponent_new_from_string (calobj);
	g_assert_nonnull (icalcomp);
	g_assert_cmpint (icalcomponent_isa (icalcomp), ==, ICAL_VEVENT_COMPONENT);
	icalcomponent_free (icalcomp);

	g_free (calobj);
	calobj = NULL;

	/* Going offline */
	e_cal_meta_backend_test_change_online (meta_backend, FALSE);

	g_assert (!e_cal_cache_contains (cal_cache, "event-7", NULL, E_CACHE_EXCLUDE_DELETED));

	e_cal_meta_backend_test_reset_counters (test_backend);

	backend_class->get_object_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, "event-7", NULL, &calobj, &error);
	g_assert_error (error, E_DATA_CAL_ERROR, ObjectNotFound);
	g_assert_null (calobj);
	g_clear_error (&error);
	g_assert_cmpint (test_backend->connect_count, ==, 0);
	g_assert_cmpint (test_backend->list_count, ==, 0);
	g_assert_cmpint (test_backend->load_count, ==, 0);

	/* Going online */
	e_cal_meta_backend_test_change_online (meta_backend, TRUE);

	g_assert (e_cal_cache_contains (cal_cache, "event-7", NULL, E_CACHE_EXCLUDE_DELETED));

	/* Remove it from the cache, thus it's loaded from the "server" on demand */
	e_cal_cache_remove_component (cal_cache, "event-7", NULL, E_CACHE_IS_ONLINE, NULL, &error);
	g_assert_no_error (error);

	g_assert_cmpint (test_backend->connect_count, ==, 1);
	e_cal_meta_backend_test_reset_counters (test_backend);
	g_assert_cmpint (test_backend->connect_count, ==, 0);

	backend_class->get_object_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, "event-7", NULL, &calobj, &error);
	g_assert_no_error (error);
	g_assert_nonnull (calobj);
	g_assert_cmpint (test_backend->connect_count, ==, 0);
	g_assert_cmpint (test_backend->list_count, ==, 0);
	g_assert_cmpint (test_backend->load_count, ==, 1);
	g_assert_nonnull (strstr (calobj, "UID:event-7"));
	g_free (calobj);

	g_assert (e_cal_cache_contains (cal_cache, "event-7", NULL, E_CACHE_EXCLUDE_DELETED));

	g_object_unref (cal_cache);
}

static void
test_get_object_list (ECalMetaBackend *meta_backend)
{
	ECalBackendSyncClass *backend_class;
	GSList *calobjs = NULL;
	GError *error = NULL;

	g_assert_nonnull (meta_backend);

	backend_class = E_CAL_BACKEND_SYNC_GET_CLASS (meta_backend);
	g_return_if_fail (backend_class != NULL);
	g_return_if_fail (backend_class->get_object_list_sync != NULL);

	backend_class->get_object_list_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, "(uid? \"unknown-event\")", &calobjs, &error);
	g_assert_no_error (error);
	g_assert_null (calobjs);

	backend_class->get_object_list_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, "(uid? \"event-3\")", &calobjs, &error);
	g_assert_no_error (error);
	g_assert_nonnull (calobjs);
	g_assert_cmpint (g_slist_length (calobjs), ==, 1);
	g_assert (strstr (calobjs->data, "UID:event-3"));
	g_slist_free_full (calobjs, g_free);
	calobjs = NULL;

	backend_class->get_object_list_sync (E_CAL_BACKEND_SYNC (meta_backend),
		NULL, NULL, "(uid? \"event-6\")", &calobjs, &error);
	g_assert_no_error (error);
	g_assert_nonnull (calobjs);
	g_assert_cmpint (g_slist_length (calobjs), ==, 2);
	g_assert (strstr (calobjs->data, "UID:event-6"));
	g_assert (strstr (calobjs->next->data, "UID:event-6"));
	g_assert_cmpstr (calobjs->data, !=, calobjs->next->data);
	g_slist_free_full (calobjs, g_free);
}

static void
test_refresh (ECalMetaBackend *meta_backend)
{
	ECalMetaBackendTest *test_backend;
	ECalCache *cal_cache;
	ECache *cache;
	guint count;
	icalcomponent *icalcomp;
	gchar *sync_tag;
	GError *error = NULL;

	g_assert_nonnull (meta_backend);

	test_backend = E_CAL_META_BACKEND_TEST (meta_backend);
	cal_cache = e_cal_meta_backend_ref_cache (meta_backend);
	g_assert_nonnull (cal_cache);

	cache = E_CACHE (cal_cache);

	/* Empty local cache */
	e_cache_remove_all (cache, NULL, &error);
	g_assert_no_error (error);

	count = e_cache_get_count (cache, E_CACHE_INCLUDE_DELETED, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpint (count, ==, 0);

	e_cal_meta_backend_test_reset_counters (test_backend);

	ecmb_test_remove_component (test_backend, "event-6", "20170225T134900");
	ecmb_test_remove_component (test_backend, "event-7", NULL);
	ecmb_test_remove_component (test_backend, "event-8", NULL);
	ecmb_test_remove_component (test_backend, "event-9", NULL);

	/* Sync with server content */
	e_cal_meta_backend_test_call_refresh (meta_backend);

	g_assert_cmpint (test_backend->list_count, ==, 1);
	g_assert_cmpint (test_backend->save_count, ==, 0);
	g_assert_cmpint (test_backend->load_count, ==, 6);
	g_assert_cmpint (test_backend->remove_count, ==, 0);

	count = e_cache_get_count (cache, E_CACHE_INCLUDE_DELETED, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpint (count, ==, 6);

	ecmb_test_cache_and_server_equal (cal_cache, test_backend->vcalendar, E_CACHE_INCLUDE_DELETED);

	sync_tag = e_cal_meta_backend_dup_sync_tag (meta_backend);
	g_assert_nonnull (sync_tag);
	g_assert_cmpstr (sync_tag, ==, "1");
	g_free (sync_tag);

	/* Add detached instance, but do not modify the master object, thus it looks like unchanged */
	ecmb_test_add_test_case (test_backend, "event-6-a");

	/* Sync with server content */
	e_cal_meta_backend_test_call_refresh (meta_backend);

	g_assert_cmpint (test_backend->list_count, ==, 2);
	g_assert_cmpint (test_backend->save_count, ==, 0);
	g_assert_cmpint (test_backend->load_count, ==, 6);
	g_assert_cmpint (test_backend->remove_count, ==, 0);

	count = e_cache_get_count (cache, E_CACHE_INCLUDE_DELETED, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpint (count, ==, 6);

	ecmb_test_vcalendar_contains (test_backend->vcalendar, FALSE, TRUE,
		"event-1", NULL,
		"event-2", NULL,
		"event-3", NULL,
		"event-4", NULL,
		"event-5", NULL,
		"event-6", NULL,
		"event-6", "20170225T134900",
		NULL);

	ecmb_test_cache_contains (cal_cache, FALSE, TRUE,
		"event-1", NULL,
		"event-2", NULL,
		"event-3", NULL,
		"event-4", NULL,
		"event-5", NULL,
		"event-6", NULL,
		NULL);

	/* Modify the master object, thus the detached instance will be recognized */
	for (icalcomp = icalcomponent_get_first_component (test_backend->vcalendar, ICAL_VEVENT_COMPONENT);
	     icalcomp;
	     icalcomp = icalcomponent_get_next_component (test_backend->vcalendar, ICAL_VEVENT_COMPONENT)) {
		if (g_strcmp0 ("event-6", icalcomponent_get_uid (icalcomp)) == 0) {
			icalcomponent_set_sequence (icalcomp, icalcomponent_get_sequence (icalcomp) + 1);
		}
	}

	/* Sync with server content */
	e_cal_meta_backend_test_call_refresh (meta_backend);

	g_assert_cmpint (test_backend->list_count, ==, 3);
	g_assert_cmpint (test_backend->save_count, ==, 0);
	g_assert_cmpint (test_backend->load_count, ==, 7);
	g_assert_cmpint (test_backend->remove_count, ==, 0);

	count = e_cache_get_count (cache, E_CACHE_INCLUDE_DELETED, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpint (count, ==, 7);

	ecmb_test_cache_and_server_equal (cal_cache, test_backend->vcalendar, E_CACHE_INCLUDE_DELETED);

	/* Add some more events */
	ecmb_test_add_test_case (test_backend, "event-7");
	ecmb_test_add_test_case (test_backend, "event-9");

	/* Sync with server content */
	e_cal_meta_backend_test_call_refresh (meta_backend);

	g_assert_cmpint (test_backend->list_count, ==, 4);
	g_assert_cmpint (test_backend->save_count, ==, 0);
	g_assert_cmpint (test_backend->load_count, ==, 9);
	g_assert_cmpint (test_backend->remove_count, ==, 0);

	count = e_cache_get_count (cache, E_CACHE_INCLUDE_DELETED, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpint (count, ==, 9);

	ecmb_test_cache_and_server_equal (cal_cache, test_backend->vcalendar, E_CACHE_INCLUDE_DELETED);

	/* Remove two events */
	ecmb_test_remove_component (test_backend, "event-2", NULL);
	ecmb_test_remove_component (test_backend, "event-4", NULL);

	/* Sync with server content */
	e_cal_meta_backend_test_call_refresh (meta_backend);

	g_assert_cmpint (test_backend->list_count, ==, 5);
	g_assert_cmpint (test_backend->save_count, ==, 0);
	g_assert_cmpint (test_backend->load_count, ==, 9);
	g_assert_cmpint (test_backend->remove_count, ==, 0);

	count = e_cache_get_count (cache, E_CACHE_INCLUDE_DELETED, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpint (count, ==, 7);

	ecmb_test_cache_and_server_equal (cal_cache, test_backend->vcalendar, E_CACHE_INCLUDE_DELETED);

	/* Mix add/remove/modify */
	ecmb_test_add_test_case (test_backend, "event-8");

	ecmb_test_remove_component (test_backend, "event-3", NULL);
	ecmb_test_remove_component (test_backend, "event-6", NULL);
	ecmb_test_remove_component (test_backend, "event-6", "20170225T134900");

	for (icalcomp = icalcomponent_get_first_component (test_backend->vcalendar, ICAL_VEVENT_COMPONENT);
	     icalcomp;
	     icalcomp = icalcomponent_get_next_component (test_backend->vcalendar, ICAL_VEVENT_COMPONENT)) {
		if (g_strcmp0 ("event-5", icalcomponent_get_uid (icalcomp)) == 0 ||
		    g_strcmp0 ("event-9", icalcomponent_get_uid (icalcomp)) == 0) {
			icalcomponent_set_sequence (icalcomp, icalcomponent_get_sequence (icalcomp) + 1);
		}
	}

	/* Sync with server content */
	e_cal_meta_backend_test_call_refresh (meta_backend);

	g_assert_cmpint (test_backend->list_count, ==, 6);
	g_assert_cmpint (test_backend->save_count, ==, 0);
	g_assert_cmpint (test_backend->load_count, ==, 12);
	g_assert_cmpint (test_backend->remove_count, ==, 0);

	count = e_cache_get_count (cache, E_CACHE_INCLUDE_DELETED, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpint (count, ==, 5);

	ecmb_test_cache_and_server_equal (cal_cache, test_backend->vcalendar, E_CACHE_INCLUDE_DELETED);

	sync_tag = e_cal_meta_backend_dup_sync_tag (meta_backend);
	g_assert_nonnull (sync_tag);
	g_assert_cmpstr (sync_tag, ==, "7");
	g_free (sync_tag);

	g_object_unref (cal_cache);
}

typedef void (* TestWithMainLoopFunc) (ECalMetaBackend *meta_backend);

typedef struct _MainLoopThreadData {
	TestWithMainLoopFunc func;
	ECalMetaBackend *meta_backend;
	GMainLoop *main_loop;
} MainLoopThreadData;

static gpointer
test_with_main_loop_thread (gpointer user_data)
{
	MainLoopThreadData *mlt = user_data;

	g_assert_nonnull (mlt);
	g_assert_nonnull (mlt->func);
	g_assert_nonnull (mlt->meta_backend);

	mlt->func (mlt->meta_backend);

	g_main_loop_quit (mlt->main_loop);

	return NULL;
}

static gboolean
quit_test_with_mainloop_cb (gpointer user_data)
{
	GMainLoop *main_loop = user_data;

	g_assert_nonnull (main_loop);

	g_main_loop_quit (main_loop);

	g_assert_not_reached ();

	return FALSE;
}

static gboolean
test_with_mainloop_run_thread_idle (gpointer user_data)
{
	GThread *thread;

	g_assert_nonnull (user_data);

	thread = g_thread_new (NULL, test_with_main_loop_thread, user_data);
	g_thread_unref (thread);

	return FALSE;
}

static void
test_with_main_loop (ECalCache *cal_cache,
		     TestWithMainLoopFunc func)
{
	MainLoopThreadData mlt;
	ECalMetaBackend *meta_backend;
	guint timeout_id;

	g_assert_nonnull (cal_cache);
	g_assert_nonnull (func);

	meta_backend = e_cal_meta_backend_test_new (cal_cache);
	g_assert_nonnull (meta_backend);

	mlt.func = func;
	mlt.meta_backend = meta_backend;
	mlt.main_loop = g_main_loop_new (NULL, FALSE);

	g_idle_add (test_with_mainloop_run_thread_idle, &mlt);
	timeout_id = g_timeout_add_seconds (10, quit_test_with_mainloop_cb, mlt.main_loop);

	g_main_loop_run (mlt.main_loop);

	g_source_remove (timeout_id);
	g_main_loop_unref (mlt.main_loop);
	g_clear_object (&mlt.meta_backend);
}

#define main_loop_wrapper(_func) \
static void \
_func ## _tcu (TCUFixture *fixture, \
	       gconstpointer user_data) \
{ \
	test_with_main_loop (fixture->cal_cache, _func); \
}

main_loop_wrapper (test_send_objects)
main_loop_wrapper (test_get_attachment_uris)
main_loop_wrapper (test_discard_alarm)
main_loop_wrapper (test_timezones)
main_loop_wrapper (test_get_free_busy)
main_loop_wrapper (test_create_objects)
main_loop_wrapper (test_modify_objects)
main_loop_wrapper (test_remove_objects)
main_loop_wrapper (test_receive_objects)
main_loop_wrapper (test_receive_and_remove)
main_loop_wrapper (test_get_object)
main_loop_wrapper (test_get_object_list)
main_loop_wrapper (test_refresh)

#undef main_loop_wrapper

gint
main (gint argc,
      gchar **argv)
{
	ETestServerClosure tsclosure = {
		E_TEST_SERVER_NONE,
		NULL, /* Source customization function */
		0,    /* Calendar Type */
		TRUE, /* Keep the working sandbox after the test, don't remove it */
		NULL, /* Destroy Notify function */
	};
	ETestServerFixture tsfixture = { 0 };
	TCUClosure closure_events = { TCU_LOAD_COMPONENT_SET_EVENTS };
	gint res;

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

	e_test_server_utils_prepare_run (0);
	e_test_server_utils_setup (&tsfixture, &tsclosure);

	glob_registry = tsfixture.registry;
	g_assert_nonnull (glob_registry);

	g_test_add ("/ECalMetaBackend/MergeInstances", TCUFixture, &closure_events,
		tcu_fixture_setup, test_merge_instances, tcu_fixture_teardown);
	g_test_add ("/ECalMetaBackend/Attachments", TCUFixture, &closure_events,
		tcu_fixture_setup, test_attachments, tcu_fixture_teardown);
	g_test_add ("/ECalMetaBackend/EmptyCache", TCUFixture, &closure_events,
		tcu_fixture_setup, test_empty_cache, tcu_fixture_teardown);
	g_test_add ("/ECalMetaBackend/SendObjects", TCUFixture, &closure_events,
		tcu_fixture_setup, test_send_objects_tcu, tcu_fixture_teardown);
	g_test_add ("/ECalMetaBackend/GetAttachmentUris", TCUFixture, &closure_events,
		tcu_fixture_setup, test_get_attachment_uris_tcu, tcu_fixture_teardown);
	g_test_add ("/ECalMetaBackend/DiscardAlarm", TCUFixture, &closure_events,
		tcu_fixture_setup, test_discard_alarm_tcu, tcu_fixture_teardown);
	g_test_add ("/ECalMetaBackend/Timezones", TCUFixture, &closure_events,
		tcu_fixture_setup, test_timezones_tcu, tcu_fixture_teardown);
	g_test_add ("/ECalMetaBackend/GetFreeBusy", TCUFixture, &closure_events,
		tcu_fixture_setup, test_get_free_busy_tcu, tcu_fixture_teardown);
	g_test_add ("/ECalMetaBackend/CreateObjects", TCUFixture, &closure_events,
		tcu_fixture_setup, test_create_objects_tcu, tcu_fixture_teardown);
	g_test_add ("/ECalMetaBackend/ModifyObjects", TCUFixture, &closure_events,
		tcu_fixture_setup, test_modify_objects_tcu, tcu_fixture_teardown);
	g_test_add ("/ECalMetaBackend/RemoveObjects", TCUFixture, &closure_events,
		tcu_fixture_setup, test_remove_objects_tcu, tcu_fixture_teardown);
	g_test_add ("/ECalMetaBackend/ReceiveObjects", TCUFixture, &closure_events,
		tcu_fixture_setup, test_receive_objects_tcu, tcu_fixture_teardown);
	g_test_add ("/ECalMetaBackend/ReceiveAndRemove", TCUFixture, &closure_events,
		tcu_fixture_setup, test_receive_and_remove_tcu, tcu_fixture_teardown);
	g_test_add ("/ECalMetaBackend/GetObject", TCUFixture, &closure_events,
		tcu_fixture_setup, test_get_object_tcu, tcu_fixture_teardown);
	g_test_add ("/ECalMetaBackend/GetObjectList", TCUFixture, &closure_events,
		tcu_fixture_setup, test_get_object_list_tcu, tcu_fixture_teardown);
	g_test_add ("/ECalMetaBackend/Refresh", TCUFixture, &closure_events,
		tcu_fixture_setup, test_refresh_tcu, tcu_fixture_teardown);

	res = g_test_run ();

	e_test_server_utils_teardown (&tsfixture, &tsclosure);
	e_test_server_utils_finish_run ();

	return res;
}
