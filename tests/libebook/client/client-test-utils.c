/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdio.h>

#include "client-test-utils.h"

void
report_error (const gchar *operation, GError **error)
{
	g_return_if_fail (operation != NULL);

	g_printerr ("Failed to %s: %s\n", operation, (error && *error) ? (*error)->message : "Unknown error");

	g_clear_error (error);
}

void
print_email (EContact *contact)
{
	const gchar *file_as = e_contact_get_const (contact, E_CONTACT_FILE_AS);
	const gchar *name_or_org = e_contact_get_const (contact, E_CONTACT_NAME_OR_ORG);
	GList *emails, *e;

	g_print ("   Contact: %s\n", file_as);
	g_print ("   Name or org: %s\n", name_or_org);
	g_print ("   Email addresses:\n");
	emails = e_contact_get (contact, E_CONTACT_EMAIL);
	for (e = emails; e; e = e->next) {
		g_print ("\t%s\n",  (gchar *)e->data);
	}
	g_list_foreach (emails, (GFunc)g_free, NULL);
	g_list_free (emails);

	g_print ("\n");
}

EBookClient *
open_system_book (gboolean only_if_exists)
{
	EBookClient *book_client;
	GError *error = NULL;

	main_initialize ();

	book_client = e_book_client_new_system (&error);
	if (error) {
		report_error ("create system addressbook", &error);
		return NULL;
	}

	if (!e_client_open_sync (E_CLIENT (book_client), only_if_exists, NULL, &error)) {
		g_object_unref (book_client);
		report_error ("open client sync", &error);
		return NULL;
	}

	return book_client;
}

void
main_initialize (void)
{
	static gboolean initialized = FALSE;

	if (initialized)
		return;

	g_type_init ();
	g_thread_init (NULL);

	initialized = TRUE;
}

struct IdleData {
	GThreadFunc func;
	gpointer data;
	gboolean run_in_thread; /* FALSE to run in idle callback */
};

static gboolean
idle_cb (gpointer data)
{
	struct IdleData *idle = data;

	g_return_val_if_fail (idle != NULL, FALSE);
	g_return_val_if_fail (idle->func != NULL, FALSE);

	if (idle->run_in_thread) {
		GError *error = NULL;

		g_thread_create (idle->func, idle->data, FALSE, &error);

		if (error) {
			report_error ("create thread", &error);
			stop_main_loop (1);
		}
	} else {
		idle->func (idle->data);
	}

	g_free (idle);

	return FALSE;
}

static GMainLoop *loop = NULL;
static gint main_stop_result = 0;

static void
do_start (GThreadFunc func, gpointer data)
{
	main_initialize ();

	g_return_if_fail (loop == NULL);

	loop = g_main_loop_new (NULL, FALSE);

	if (func)
		func (data);

	g_main_loop_run (loop);

	g_main_loop_unref (loop);
	loop = NULL;
}

/* Starts new main-loop, but just before that calls 'func'.
   Main-loop is kept running, and this function blocks, 
   until call of stop_main_loop().
*/
void
start_main_loop (GThreadFunc func, gpointer data)
{
	g_return_if_fail (loop == NULL);

	do_start (func, data);
}

/* Starts new main-loop and then invokes func in a new thread.
   Main-loop is kept running, and this function blocks, 
   until call of stop_main_loop().
*/
void
start_in_thread_with_main_loop (GThreadFunc func, gpointer data)
{
	struct IdleData *idle;

	g_return_if_fail (func != NULL);
	g_return_if_fail (loop == NULL);

	main_initialize ();

	idle = g_new0 (struct IdleData, 1);
	idle->func = func;
	idle->data = data;
	idle->run_in_thread = TRUE;

	g_idle_add (idle_cb, idle);

	do_start (NULL, NULL);
}

/* Starts new main-loop and then invokes func in an idle callback.
   Main-loop is kept running, and this function blocks, 
   until call of stop_main_loop().
*/
void
start_in_idle_with_main_loop (GThreadFunc func, gpointer data)
{
	struct IdleData *idle;

	g_return_if_fail (func != NULL);
	g_return_if_fail (loop == NULL);

	main_initialize ();

	idle = g_new0 (struct IdleData, 1);
	idle->func = func;
	idle->data = data;
	idle->run_in_thread = FALSE;

	g_idle_add (idle_cb, idle);

	do_start (NULL, NULL);
}

/* Stops main-loop previously run by start_main_loop,
   start_in_thread_with_main_loop or start_in_idle_with_main_loop.
*/
void
stop_main_loop (gint stop_result)
{
	g_return_if_fail (loop != NULL);

	main_stop_result = stop_result;
	g_main_loop_quit (loop);
}

/* returns value used in stop_main_loop() */
gint
get_main_loop_stop_result (void)
{
	return main_stop_result;
}

void
foreach_configured_source (void (*func) (ESource *source))
{
	gpointer foreach_async_data;
	ESource *source = NULL;

	g_return_if_fail (func != NULL);

	main_initialize ();

	foreach_async_data = foreach_configured_source_async_start (&source);
	if (!foreach_async_data)
		return;

	do {
		func (source);
	} while (foreach_configured_source_async_next (&foreach_async_data, &source));
}

struct ForeachConfiguredData
{
	ESourceList *source_list;
	GSList *current_group;
	GSList *current_source;
};

gpointer
foreach_configured_source_async_start (ESource **source)
{
	struct ForeachConfiguredData *async_data;
	ESourceList *source_list = NULL;
	GError *error = NULL;

	g_return_val_if_fail (source != NULL, NULL);

	main_initialize ();

	if (!e_book_client_get_sources (&source_list, &error)) {
		report_error ("get addressbooks", &error);
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

gboolean
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

EBookClient *
new_temp_client (gchar **uri)
{
	EBookClient *book_client;
	ESource *source;
	gchar *abs_uri, *filename;
	gint handle;
	GError *error = NULL;

	filename = g_build_filename (g_get_tmp_dir (), "e-book-client-test-XXXXXX/", NULL);
	handle = g_mkstemp (filename);

	if (handle != -1)
		close (handle);

	g_return_val_if_fail (g_mkdir_with_parents (filename, 0700) == 0, NULL);

	abs_uri = g_strconcat ("local://", filename, NULL);
	g_free (filename);

	source = e_source_new_with_absolute_uri ("Test book", abs_uri);
	if (uri)
		*uri = abs_uri;
	else
		g_free (abs_uri);

	g_return_val_if_fail (source != NULL, NULL);

	book_client = e_book_client_new (source, &error);
	g_object_unref (source);

	if (error)
		report_error ("new temp client", &error);

	return book_client;
}

gchar *
new_vcard_from_test_case (const gchar *case_name)
{
	gchar *filename;
	gchar *case_filename;
	GFile* file;
	GError *error = NULL;
	gchar *vcard;

        case_filename = g_strdup_printf ("%s.vcf", case_name);
	filename = g_build_filename (SRCDIR, "..", "data", "vcards", case_filename, NULL);
	file = g_file_new_for_path (filename);
	if (!g_file_load_contents (file, NULL, &vcard, NULL, NULL, &error)) {
                g_warning ("failed to read test contact file '%s': %s",
				filename, error->message);
		exit (1);
	}

	g_free (case_filename);
	g_free (filename);
	g_object_unref (file);

	return vcard;
}

static gboolean
contacts_are_equal_shallow (EContact *a, EContact *b)
{
	const gchar *uid_a, *uid_b;

        /* Avoid warnings if one or more are NULL, to make this function
         * "NULL-friendly" */
	if (!a && !b)
		return TRUE;

	if (!E_IS_CONTACT (a) || !E_IS_CONTACT (b))
		return FALSE;

	uid_a = e_contact_get_const (a, E_CONTACT_UID);
	uid_b = e_contact_get_const (b, E_CONTACT_UID);

	return g_strcmp0 (uid_a, uid_b) == 0;
}

gboolean
add_contact_from_test_case_verify (EBookClient *book_client, const gchar *case_name, EContact **contact)
{
	gchar *vcard;
	EContact *contact_orig;
	EContact *contact_final;
	gchar *uid;
	GError *error = NULL;

	vcard = new_vcard_from_test_case (case_name);
	contact_orig = e_contact_new_from_vcard (vcard);
	g_free (vcard);
	if (!e_book_client_add_contact_sync (book_client, contact_orig, &uid, NULL, &error)) {
		report_error ("add contact sync", &error);
		g_object_unref (contact_orig);
		return FALSE;
	}

	e_contact_set (contact_orig, E_CONTACT_UID, uid);

	if (!e_book_client_get_contact_sync (book_client, uid, &contact_final, NULL, &error)) {
		report_error ("get contact sync", &error);
		g_object_unref (contact_orig);
		g_free (uid);
		return FALSE;
	}

        /* verify the contact was added "successfully" (not thorough) */
	g_assert (contacts_are_equal_shallow (contact_orig, contact_final));

	if (contact)
                *contact = contact_final;
	else
		g_object_unref (contact_final);
	g_object_unref (contact_orig);
	g_free (uid);

	return TRUE;
}
