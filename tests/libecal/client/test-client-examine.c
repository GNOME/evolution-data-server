/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <string.h>
#include <libecal/e-cal-client.h>
#include <libedataserver/e-source-group.h>

#include "client-test-utils.h"

static gint running_async = 0;

static GSList *
get_known_prop_names (void)
{
	GSList *prop_names = NULL;

	prop_names = g_slist_append (prop_names, (gpointer) CLIENT_BACKEND_PROPERTY_OPENED);
	prop_names = g_slist_append (prop_names, (gpointer) CLIENT_BACKEND_PROPERTY_OPENING);
	prop_names = g_slist_append (prop_names, (gpointer) CLIENT_BACKEND_PROPERTY_ONLINE);
	prop_names = g_slist_append (prop_names, (gpointer) CLIENT_BACKEND_PROPERTY_READONLY);
	prop_names = g_slist_append (prop_names, (gpointer) CLIENT_BACKEND_PROPERTY_CACHE_DIR);
	prop_names = g_slist_append (prop_names, (gpointer) CLIENT_BACKEND_PROPERTY_CAPABILITIES);
	prop_names = g_slist_append (prop_names, (gpointer) CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS);
	prop_names = g_slist_append (prop_names, (gpointer) CAL_BACKEND_PROPERTY_ALARM_EMAIL_ADDRESS);
	prop_names = g_slist_append (prop_names, (gpointer) CAL_BACKEND_PROPERTY_DEFAULT_OBJECT);

	return prop_names;
}

typedef struct _ExtraValues {
	gpointer async_data;

	GSList *todo_prop_names;
	GHashTable *retrieved_props;
	const gchar *cache_dir;
	icalcomponent *default_object;
} ExtraValues;

static void
extra_values_free (ExtraValues *evals)
{
	if (!evals)
		return;

	if (evals->default_object)
		icalcomponent_free (evals->default_object);
	g_slist_free (evals->todo_prop_names);
	g_hash_table_destroy (evals->retrieved_props);
	g_free (evals);
}

static void
print_with_prefix (const gchar *str, const gchar *prefix)
{
	const gchar *p = str, *n;
	while (n = strchr (p, '\n'), p) {
		if (!n) {
			g_print ("%s%s\n", prefix, p);
			break;
		} else {
			g_print ("%s%.*s\n", prefix, (gint) (n - p), p);
			n++;
		}

		p = n;
	}
}

static void
print_each_property (gpointer prop_name, gpointer prop_value, gpointer user_data)
{
	g_return_if_fail (prop_name != NULL);

	if (prop_value == NULL) {
		g_print ("\t   %s: NULL\n", (const gchar *) prop_name);
		return;
	}

	g_print ("\t   %s: ", (const gchar *) prop_name);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		GSList *values = e_client_util_parse_comma_strings (prop_value), *v;

		
		for (v = values; v; v = v->next) {
			if (v != values)
				g_print (", ");

			g_print ("'%s'", (const gchar *) v->data);
		}

		e_client_util_free_string_slist (values);
	} else if (g_str_equal (prop_name, CAL_BACKEND_PROPERTY_DEFAULT_OBJECT)) {
		g_print ("\n");
		print_with_prefix (prop_value, "\t\t");
	} else {
		g_print ("'%s'", (const gchar *) prop_value);
	}

	g_print ("\n");
}

static void
print_values (const ExtraValues *evals, EClient *client)
{
	const GSList *values;

	g_return_if_fail (evals != NULL);

	g_print ("\treadonly:%s\n", e_client_is_readonly (client) ? "yes" : "no");
	g_print ("\tonline:%s\n", e_client_is_online (client) ? "yes" : "no");
	g_print ("\topened:%s\n", e_client_is_opened (client) ? "yes" : "no");
	g_print ("\tcache dir: %s%s%s\n", evals->cache_dir ? "'" : "", evals->cache_dir ? evals->cache_dir : "none", evals->cache_dir ? "'" : "");
	g_print ("\tcapabilities: ");
	values = e_client_get_capabilities (client);
	if (!values) {
		g_print ("NULL");
	} else {
		while (values) {
			const gchar *cap = values->data;

			g_print ("'%s'", cap);
			if (!e_client_check_capability (client, cap))
				g_print (" (not found in EClient)");

			values = values->next;

			if (values)
				g_print (", ");
		}
	}
	g_print ("\n");

	g_print ("\tdefault object: %s\n", evals->default_object ? "" : "none");
	if (evals->default_object) {
		gchar *comp_str = icalcomponent_as_ical_string_r (evals->default_object);
		print_with_prefix (comp_str, "\t   ");
		g_free (comp_str);
	}

	g_print ("\tbackend properties:\n");
	g_hash_table_foreach (evals->retrieved_props, print_each_property, NULL);
}

static void
identify_source (ESource *source, ECalClientSourceType source_type)
{
	const gchar *name, *uri, *type;
	gchar *abs_uri = NULL;

	g_return_if_fail (source != NULL);

	switch (source_type) {
	case E_CAL_CLIENT_SOURCE_TYPE_EVENTS:
		type = "events";
		break;
	case E_CAL_CLIENT_SOURCE_TYPE_TASKS:
		type = "tasks";
		break;
	case E_CAL_CLIENT_SOURCE_TYPE_MEMOS:
		type = "memos";
		break;
	default:
		type = "unknown-type";
		break;
	}

	name = e_source_peek_name (source);
	if (!name)
		name = "Unknown name";

	uri = e_source_peek_absolute_uri (source);
	if (!uri) {
		abs_uri = e_source_build_absolute_uri (source);
		uri = abs_uri;
	}
	if (!uri)
		uri = e_source_peek_relative_uri (source);
	if (!uri)
		uri = "Unknown uri";

	g_print ("\n   Checking %s source '%s' (%s)\n", type, name, uri);

	g_free (abs_uri);
}

static void
identify_client (ECalClient *cal_client)
{
	g_return_if_fail (cal_client != NULL);
	g_return_if_fail (E_IS_CAL_CLIENT (cal_client));

	identify_source (e_client_get_source (E_CLIENT (cal_client)), e_cal_client_get_source_type (cal_client));
}

static void client_opened_async (GObject *source_object, GAsyncResult *result, gpointer async_data);

static void
continue_next_source (gpointer async_data)
{
	ESource *source = NULL;
	ECalClient *cal_client;
	GError *error = NULL;

	g_return_if_fail (async_data != NULL);

	while (async_data && foreach_configured_source_async_next (&async_data, &source)) {
		ECalClientSourceType source_type = foreach_configured_source_async_get_source_type (async_data);

		cal_client = e_cal_client_new (source, source_type, &error);
		if (!cal_client) {
			identify_source (source, source_type);
			report_error ("cal client new", &error);
			continue;
		}

		e_client_open (E_CLIENT (cal_client), TRUE, NULL, client_opened_async, async_data);
		break;
	}

	if (!async_data) {
		running_async--;
		if (!running_async)
			stop_main_loop (0);
	}
}

static void
client_got_backend_property_async (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	ExtraValues *evals = user_data;
	gchar *prop_value = NULL;
	GError *error = NULL;
	ECalClient *cal_client;

	g_return_if_fail (source_object != NULL);
	g_return_if_fail (E_IS_CAL_CLIENT (source_object));
	g_return_if_fail (evals != NULL);

	cal_client = E_CAL_CLIENT (source_object);

	if (!e_client_get_backend_property_finish (E_CLIENT (cal_client), result, &prop_value, &error)) {
		identify_client (cal_client);
		report_error ("get backend property finish", &error);
	}

	g_hash_table_insert (evals->retrieved_props, evals->todo_prop_names->data, prop_value);
	evals->todo_prop_names = g_slist_remove (evals->todo_prop_names, evals->todo_prop_names->data);

	if (!evals->todo_prop_names) {
		evals->cache_dir = e_cal_client_get_local_attachment_store (cal_client);

		/* to cache them, as it can be fetched with idle as well */
		e_client_get_capabilities (E_CLIENT (source_object));

		identify_client (cal_client);
		print_values (evals, E_CLIENT (source_object));

		g_object_unref (source_object);

		continue_next_source (evals->async_data);
		extra_values_free (evals);
	} else {
		e_client_get_backend_property (E_CLIENT (cal_client), evals->todo_prop_names->data, NULL, client_got_backend_property_async, evals);
	}
}

static void
client_set_backend_property_async (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	ExtraValues *evals = user_data;
	GError *error = NULL;
	ECalClient *cal_client;

	g_return_if_fail (source_object != NULL);
	g_return_if_fail (E_IS_CAL_CLIENT (source_object));
	g_return_if_fail (evals != NULL);

	cal_client = E_CAL_CLIENT (source_object);

	if (!e_client_set_backend_property_finish (E_CLIENT (cal_client), result, &error)) {
		/* it may fail on the set_backend_property */
		g_clear_error (&error);
	} else {
		identify_client (cal_client);
		g_printerr ("   Might fail on set_backend_property, but reported success\n");
	}

	e_client_get_backend_property (E_CLIENT (cal_client), evals->todo_prop_names->data, NULL, client_got_backend_property_async, evals);
}

static void
client_got_default_object_async (GObject *source_object, GAsyncResult *result, gpointer evals_data)
{
	ExtraValues *evals = evals_data;
	ECalClient *cal_client;
	GError *error = NULL;

	g_return_if_fail (source_object != NULL);
	g_return_if_fail (E_IS_CAL_CLIENT (source_object));
	g_return_if_fail (evals != NULL);

	cal_client = E_CAL_CLIENT (source_object);

	if (!e_cal_client_get_default_object_finish (cal_client, result, &evals->default_object, &error)) {
		identify_client (cal_client);
		report_error ("get default object finish", &error);
	}

	e_client_set_backend_property (E_CLIENT (cal_client), "*unknown*property*", "*value*", NULL, client_set_backend_property_async, evals);
}

static void
client_opened_async (GObject *source_object, GAsyncResult *result, gpointer async_data)
{
	ExtraValues *evals;
	GError *error = NULL;
	ECalClient *cal_client;

	g_return_if_fail (source_object != NULL);
	g_return_if_fail (E_IS_CAL_CLIENT (source_object));
	g_return_if_fail (async_data != NULL);

	cal_client = E_CAL_CLIENT (source_object);

	if (!e_client_open_finish (E_CLIENT (source_object), result, &error)) {
		identify_client (cal_client);
		report_error ("client open finish", &error);
		g_object_unref (source_object);
		continue_next_source (async_data);
		return;
	}

	evals = g_new0 (ExtraValues, 1);
	evals->async_data = async_data;
	evals->todo_prop_names = get_known_prop_names ();
	evals->retrieved_props = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);
	
	e_cal_client_get_default_object (cal_client, NULL, client_got_default_object_async, evals);
}

static void
check_source_sync (ESource *source, ECalClientSourceType source_type)
{
	ECalClient *cal_client;
	GError *error = NULL;
	GSList *properties, *p;
	ExtraValues evals = { 0 };

	g_return_if_fail (source != NULL);

	identify_source (source, source_type);

	cal_client = e_cal_client_new (source, source_type, &error);
	if (!cal_client) {
		report_error ("cal client new", &error);
		return;
	}

	if (!e_client_open_sync (E_CLIENT (cal_client), TRUE, NULL, &error)) {
		report_error ("client open sync", &error);
		g_object_unref (cal_client);
		return;
	}

	if (!e_cal_client_get_default_object_sync (cal_client, &evals.default_object, NULL, &error)) {
		report_error ("get default object sync", &error);
	}

	if (!e_client_set_backend_property_sync (E_CLIENT (cal_client), "*unknown*property*", "*value*", NULL, &error)) {
		g_clear_error (&error);
	} else {
		identify_client (cal_client);
		g_printerr ("   Might fail on set_backend_property, but reported success\n");
	}

	evals.retrieved_props = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);

	properties = get_known_prop_names ();
	for (p = properties; p != NULL; p = p->next) {
		gchar *prop_value = NULL;

		if (!e_client_get_backend_property_sync (E_CLIENT (cal_client), p->data, &prop_value, NULL, &error)) {
			identify_client (cal_client);
			report_error ("get backend property sync", &error);
		} else {
			g_hash_table_insert (evals.retrieved_props, p->data, prop_value);
		}
	}
	g_slist_free (properties);

	evals.cache_dir = e_cal_client_get_local_attachment_store (cal_client);

	print_values (&evals, E_CLIENT (cal_client));

	g_hash_table_destroy (evals.retrieved_props);
	icalcomponent_free (evals.default_object);
	g_object_unref (cal_client);
}

static gboolean
foreach_async (ECalClientSourceType source_type)
{
	gpointer async_data;
	ESource *source = NULL;
	ECalClient *cal_client;
	GError *error = NULL;

	async_data = foreach_configured_source_async_start (source_type, &source);
	if (!async_data) {
		stop_main_loop (1);
		return FALSE;
	}

	running_async++;

	while (cal_client = e_cal_client_new (source, source_type, &error), !cal_client) {
		identify_source (source, source_type);
		report_error ("cal client new", &error);

		if (!foreach_configured_source_async_next (&async_data, &source)) {
			running_async--;
			if (!running_async)
				stop_main_loop (0);
			return FALSE;
		}

		identify_source (source, source_type);
	}

	e_client_open (E_CLIENT (cal_client), TRUE, NULL, client_opened_async, async_data);

	return TRUE;
}

static gboolean
in_main_thread_idle_cb (gpointer unused)
{
	g_print ("* run in main thread with mainloop running\n");
	foreach_configured_source (E_CAL_CLIENT_SOURCE_TYPE_EVENTS, check_source_sync);
	foreach_configured_source (E_CAL_CLIENT_SOURCE_TYPE_TASKS, check_source_sync);
	foreach_configured_source (E_CAL_CLIENT_SOURCE_TYPE_MEMOS, check_source_sync);
	g_print ("---------------------------------------------------------\n\n");

	g_print ("* run in main thread async\n");

	if (!foreach_async (E_CAL_CLIENT_SOURCE_TYPE_EVENTS))
		return FALSE;

	if (!foreach_async (E_CAL_CLIENT_SOURCE_TYPE_TASKS))
		return FALSE;

	if (!foreach_async (E_CAL_CLIENT_SOURCE_TYPE_MEMOS))
		return FALSE;

	return FALSE;
}

static gpointer
worker_thread (gpointer unused)
{
	g_print ("* run in dedicated thread with mainloop running\n");
	foreach_configured_source (E_CAL_CLIENT_SOURCE_TYPE_EVENTS, check_source_sync);
	foreach_configured_source (E_CAL_CLIENT_SOURCE_TYPE_TASKS, check_source_sync);
	foreach_configured_source (E_CAL_CLIENT_SOURCE_TYPE_MEMOS, check_source_sync);
	g_print ("---------------------------------------------------------\n\n");

	g_idle_add (in_main_thread_idle_cb, NULL);

	return NULL;
}

gint
main (gint argc, gchar **argv)
{
	main_initialize ();

	g_print ("* run in main thread without mainloop\n");
	foreach_configured_source (E_CAL_CLIENT_SOURCE_TYPE_EVENTS, check_source_sync);
	foreach_configured_source (E_CAL_CLIENT_SOURCE_TYPE_TASKS, check_source_sync);
	foreach_configured_source (E_CAL_CLIENT_SOURCE_TYPE_MEMOS, check_source_sync);
	g_print ("---------------------------------------------------------\n\n");

	start_in_thread_with_main_loop (worker_thread, NULL);

	return get_main_loop_stop_result ();
}
