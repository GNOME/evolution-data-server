/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <glib.h>

#include <libedataserver/e-source-group.h>
#include <libedataserverui/e-client-utils.h>
#include <libedataserverui/e-passwords.h>

static void stop_main_loop (gint stop_result);
static void report_error (const gchar *operation, GError **error);
static gpointer foreach_configured_source_async_start (ESource **source);
static gboolean foreach_configured_source_async_next (gpointer *foreach_async_data, ESource **source);
static gboolean foreach_async (void);

static gint running_async = 0;
static EClientSourceType source_type = E_CLIENT_SOURCE_TYPE_CONTACTS;

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

	return prop_names;
}

typedef struct _ExtraValues {
	gpointer async_data;

	GSList *todo_prop_names;
	GHashTable *retrieved_props;
} ExtraValues;

static void
extra_values_free (ExtraValues *evals)
{
	if (!evals)
		return;

	g_slist_free (evals->todo_prop_names);
	g_hash_table_destroy (evals->retrieved_props);
	g_free (evals);
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

	g_print ("\tbackend properties:\n");
	g_hash_table_foreach (evals->retrieved_props, print_each_property, NULL);
}

static void
identify_source (ESource *source)
{
	const gchar *name, *uri;
	gchar *abs_uri = NULL;

	g_return_if_fail (source != NULL);

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

	g_print ("\n   Checking source '%s' (%s)\n", name, uri);

	g_free (abs_uri);
}

static void client_opened_async (GObject *source_object, GAsyncResult *result, gpointer async_data);

static void
continue_next_source (gpointer async_data)
{
	ESource *source = NULL;

	g_return_if_fail (async_data != NULL);

	while (async_data && foreach_configured_source_async_next (&async_data, &source)) {
		identify_source (source);
		e_client_utils_open_new (source, source_type, TRUE,
			e_client_utils_authenticate_handler, NULL,
			NULL, client_opened_async, async_data);
		break;
	}

	if (!async_data) {
		running_async--;
		if (!running_async) {
			while (source_type++, source_type < E_CLIENT_SOURCE_TYPE_LAST) {
				if (foreach_async ())
					return;
			}

			stop_main_loop (0);
		}
	}
}

static void
client_got_backend_property_async (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	ExtraValues *evals = user_data;
	gchar *prop_value = NULL;
	GError *error = NULL;
	EClient *client;

	g_return_if_fail (source_object != NULL);
	g_return_if_fail (E_IS_CLIENT (source_object));
	g_return_if_fail (evals != NULL);

	client = E_CLIENT (source_object);

	if (!e_client_get_backend_property_finish (client, result, &prop_value, &error)) {
		report_error ("get backend property finish", &error);
	}

	g_hash_table_insert (evals->retrieved_props, evals->todo_prop_names->data, prop_value);
	evals->todo_prop_names = g_slist_remove (evals->todo_prop_names, evals->todo_prop_names->data);

	if (!evals->todo_prop_names) {
		/* to cache them, as it can be fetched with idle as well */
		e_client_get_capabilities (client);

		print_values (evals, client);

		g_object_unref (source_object);

		continue_next_source (evals->async_data);
		extra_values_free (evals);
	} else {
		e_client_get_backend_property (client, evals->todo_prop_names->data, NULL, client_got_backend_property_async, evals);
	}
}

static void
client_set_backend_property_async (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	ExtraValues *evals = user_data;
	GError *error = NULL;
	EClient *client;

	g_return_if_fail (source_object != NULL);
	g_return_if_fail (E_IS_CLIENT (source_object));
	g_return_if_fail (evals != NULL);

	client = E_CLIENT (source_object);

	if (!e_client_set_backend_property_finish (client, result, &error)) {
		/* it may fail on the set_backend_property */
		g_clear_error (&error);
	} else {
		g_printerr ("   Might fail on set_backend_property, but reported success\n");
	}

	e_client_get_backend_property (client, evals->todo_prop_names->data, NULL, client_got_backend_property_async, evals);
}

static void
client_opened_async (GObject *source_object, GAsyncResult *result, gpointer async_data)
{
	ExtraValues *evals;
	GError *error = NULL;
	EClient *client = NULL;

	g_return_if_fail (source_object == NULL);
	g_return_if_fail (async_data != NULL);

	if (!e_client_utils_open_new_finish (result, &client, &error)) {
		report_error ("client utils open new finish", &error);
		continue_next_source (async_data);
		return;
	}

	evals = g_new0 (ExtraValues, 1);
	evals->async_data = async_data;
	evals->todo_prop_names = get_known_prop_names ();
	evals->retrieved_props = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);

	e_client_set_backend_property (client, "*unknown*property*", "*value*", NULL, client_set_backend_property_async, evals);
}

static gboolean
foreach_async (void)
{
	gpointer async_data;
	ESource *source = NULL;

	async_data = foreach_configured_source_async_start (&source);
	if (!async_data) {
		stop_main_loop (1);
		return FALSE;
	}

	running_async++;

	identify_source (source);
	e_client_utils_open_new (source, source_type, TRUE,
		e_client_utils_authenticate_handler, NULL,
		NULL, client_opened_async, async_data);

	return TRUE;
}

static gboolean
in_main_thread_idle_cb (gpointer unused)
{
	if (!foreach_async ())
		return FALSE;

	return FALSE;
}

static GMainLoop *loop = NULL;
static gint main_stop_result = 0;

static void
stop_main_loop (gint stop_result)
{
	g_return_if_fail (loop != NULL);

	main_stop_result = stop_result;
	g_main_loop_quit (loop);
}

static gint
get_main_loop_stop_result (void)
{
	return main_stop_result;
}

struct ForeachConfiguredData
{
	ESourceList *source_list;
	GSList *current_group;
	GSList *current_source;
};

static gpointer
foreach_configured_source_async_start (ESource **source)
{
	struct ForeachConfiguredData *async_data;
	ESourceList *source_list = NULL;
	GError *error = NULL;

	g_return_val_if_fail (source != NULL, NULL);

	if (!e_client_utils_get_sources (&source_list, source_type, &error)) {
		report_error ("get sources", &error);
		return NULL;
	}

	g_return_val_if_fail (source_list != NULL, NULL);

	async_data = g_new0 (struct ForeachConfiguredData, 1);
	async_data->source_list = source_list;
	async_data->current_group = e_source_list_peek_groups (source_list);
	if (!async_data->current_group) {
		gpointer ad = async_data;

		foreach_configured_source_async_next (&ad, source);
		return ad;
	}

	async_data->current_source = e_source_group_peek_sources (async_data->current_group->data);
	if (!async_data->current_source) {
		gpointer ad = async_data;

		if (foreach_configured_source_async_next (&ad, source))
			return ad;

		return NULL;
	}

	*source = async_data->current_source->data;

	return async_data;
}

static gboolean
foreach_configured_source_async_next (gpointer *foreach_async_data, ESource **source)
{
	struct ForeachConfiguredData *async_data;

	g_return_val_if_fail (foreach_async_data != NULL, FALSE);
	g_return_val_if_fail (source != NULL, FALSE);

	async_data = *foreach_async_data;
	g_return_val_if_fail (async_data != NULL, FALSE);
	g_return_val_if_fail (async_data->source_list != NULL, FALSE);
	g_return_val_if_fail (async_data->current_group != NULL, FALSE);

	if (async_data->current_source)
		async_data->current_source = async_data->current_source->next;
	if (async_data->current_source) {
		*source = async_data->current_source->data;
		return TRUE;
	}

	do {
		async_data->current_group = async_data->current_group->next;
		if (async_data->current_group)
			async_data->current_source = e_source_group_peek_sources (async_data->current_group->data);
	} while (async_data->current_group && !async_data->current_source);

	if (async_data->current_source) {
		*source = async_data->current_source->data;
		return TRUE;
	}

	g_object_unref (async_data->source_list);
	g_free (async_data);

	*foreach_async_data = NULL;

	return FALSE;
}

static void
report_error (const gchar *operation, GError **error)
{
	g_return_if_fail (operation != NULL);

	g_printerr ("Failed to %s: %s\n", operation, (error && *error) ? (*error)->message : "Unknown error");

	g_clear_error (error);
}

gint
main (gint argc, gchar **argv)
{
	g_type_init ();
	g_thread_init (NULL);
	gtk_init (&argc, &argv);

	e_passwords_init ();

	g_idle_add (in_main_thread_idle_cb, NULL);

	loop = g_main_loop_new (NULL, FALSE);
	g_main_loop_run (loop);
	g_main_loop_unref (loop);

	return get_main_loop_stop_result ();
}
