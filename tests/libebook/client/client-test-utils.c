/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdio.h>
#include <stdlib.h>

#include <libedataserver/libedataserver.h>

#include "client-test-utils.h"
#include "e-test-dbus-utils.h"

/* This forces the GType to be registered in a way that
 * avoids a "statement with no effect" compiler warning.
 * FIXME Use g_type_ensure() once we require GLib 2.34. */
#define REGISTER_TYPE(type) \
	(g_type_class_unref (g_type_class_ref (type)))

void
report_error (const gchar *operation,
              GError **error)
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
		g_print ("\t%s\n",  (gchar *) e->data);
	}
	g_list_foreach (emails, (GFunc) g_free, NULL);
	g_list_free (emails);

	g_print ("\n");
}

EBookClient *
open_system_book (ESourceRegistry *registry,
                  gboolean only_if_exists)
{
	ESource *source;
	EBookClient *book_client;
	GError *error = NULL;

	main_initialize ();

	source = e_source_registry_ref_builtin_address_book (registry);
	book_client = e_book_client_new (source, &error);
	g_object_unref (source);

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
	GTestDBus *test_dbus = NULL;

	if (initialized)
		return;

	g_type_init ();
	e_gdbus_templates_init_main_thread ();

	e_test_setup_base_directories ();
	test_dbus = e_test_setup_dbus_session ();

	g_test_dbus_up (test_dbus);

	g_print ("Using private D-Bus session for testing: \"%s\"\n",
                 g_test_dbus_get_bus_address (test_dbus));

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
do_start (GThreadFunc func,
          gpointer data)
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
 * Main-loop is kept running, and this function blocks,
 * until call of stop_main_loop (). */
void
start_main_loop (GThreadFunc func,
                 gpointer data)
{
	g_return_if_fail (loop == NULL);

	do_start (func, data);
}

/* Starts new main-loop and then invokes func in a new thread.
 * Main-loop is kept running, and this function blocks,
 * until call of stop_main_loop (). */
void
start_in_thread_with_main_loop (GThreadFunc func,
                                gpointer data)
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
 * Main-loop is kept running, and this function blocks,
 * until call of stop_main_loop (). */
void
start_in_idle_with_main_loop (GThreadFunc func,
                              gpointer data)
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
 * start_in_thread_with_main_loop or start_in_idle_with_main_loop.
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
foreach_configured_source (ESourceRegistry *registry,
                           void (*func) (ESource *source))
{
	gpointer foreach_async_data;
	ESource *source = NULL;

	g_return_if_fail (func != NULL);

	main_initialize ();

	foreach_async_data = foreach_configured_source_async_start (registry, &source);
	if (!foreach_async_data)
		return;

	do {
		func (source);
	} while (foreach_configured_source_async_next (&foreach_async_data, &source));
}

struct ForeachConfiguredData {
	GList *list;
};

gpointer
foreach_configured_source_async_start (ESourceRegistry *registry,
                                       ESource **source)
{
	struct ForeachConfiguredData *async_data;
	const gchar *extension_name;
	GList *list;

	g_return_val_if_fail (source != NULL, NULL);

	main_initialize ();

	extension_name = E_SOURCE_EXTENSION_ADDRESS_BOOK;
	list = e_source_registry_list_sources (registry, extension_name);

	async_data = g_new0 (struct ForeachConfiguredData, 1);
	async_data->list = list;

	*source = async_data->list->data;

	return async_data;
}

gboolean
foreach_configured_source_async_next (gpointer *foreach_async_data,
                                      ESource **source)
{
	struct ForeachConfiguredData *async_data;

	g_return_val_if_fail (foreach_async_data != NULL, FALSE);
	g_return_val_if_fail (source != NULL, FALSE);

	async_data = *foreach_async_data;
	g_return_val_if_fail (async_data != NULL, FALSE);

	if (async_data->list) {
		g_object_unref (async_data->list->data);
		async_data->list = async_data->list->next;
	}
	if (async_data->list) {
		*source = async_data->list->data;
		return TRUE;
	}

	g_free (async_data);

	*foreach_async_data = NULL;

	return FALSE;
}



typedef struct {
	GMainLoop       *loop;
	const gchar     *uid;
	ESourceRegistry *registry;
	ESource         *scratch;
	ESource         *source;
	EBookClient     *book;
} CreateBookData;

static gboolean
quit_idle (CreateBookData *data)
{
	g_main_loop_quit (data->loop);
	return FALSE;
}

static gboolean
create_book_idle (CreateBookData *data)
{
	GError *error = NULL;

	data->source = e_source_registry_ref_source (data->registry, data->uid);
	if (!data->source)
		g_error ("Unable to fetch newly created source uid '%s' from the registry", data->uid);

	if (g_getenv ("DEBUG_DIRECT") != NULL)
		data->book = e_book_client_new_direct (data->registry, data->source, &error);
	else
		data->book = e_book_client_new (data->source, &error);

	if (!data->book)
		g_error ("Unable to create the book: %s", error->message);

	g_idle_add ((GSourceFunc)quit_idle, data);

	return FALSE;
}

static gboolean
register_source_idle (CreateBookData *data)
{
	GError *error = NULL;
	ESourceBackend  *backend;
	ESourceAddressBookConfig *config;

	data->registry = e_source_registry_new_sync (NULL, &error);
	if (!data->registry)
		g_error ("Unable to create the registry: %s", error->message);

	data->scratch = e_source_new_with_uid (data->uid, NULL, &error);
	if (!data->scratch)
		g_error ("Failed to create source with uid '%s': %s", data->uid, error->message);

	backend = e_source_get_extension (data->scratch, E_SOURCE_EXTENSION_ADDRESS_BOOK);
	e_source_backend_set_backend_name (backend, "local");

	REGISTER_TYPE (E_TYPE_SOURCE_ADDRESS_BOOK_CONFIG);
	config = e_source_get_extension (data->scratch, E_SOURCE_EXTENSION_ADDRESS_BOOK_CONFIG);
	e_source_address_book_config_set_revision_guards_enabled (config, TRUE);

	if (!e_source_registry_commit_source_sync (data->registry, data->scratch, NULL, &error))
		g_error ("Unable to add new source to the registry for uid %s: %s", data->uid, error->message);

	/* XXX e_source_registry_commit_source_sync isnt really sync... or else
	 * we could call e_source_registry_ref_source() immediately
	 */
	g_timeout_add (20, (GSourceFunc)create_book_idle, data);

	return FALSE;
}

static EBookClient *
ebook_test_utils_book_with_uid (const gchar *uid)
{
	CreateBookData data = { 0, };

	data.uid = uid;

	data.loop = g_main_loop_new (NULL, FALSE);
	g_idle_add ((GSourceFunc)register_source_idle, &data);
	g_main_loop_run (data.loop);
	g_main_loop_unref (data.loop);

	g_object_unref (data.scratch);
	g_object_unref (data.source);
	g_object_unref (data.registry);

	return data.book;
}

EBookClient *
new_temp_client (gchar **uri)
{
	EBookClient     *book;
	gchar           *uid;
	guint64          real_time = g_get_real_time ();

	uid  = g_strdup_printf ("test-book-%" G_GINT64_FORMAT, real_time);
	book = ebook_test_utils_book_with_uid (uid);

	if (uri)
		*uri = g_strdup (uid);

	g_free (uid);

	return book;
}

gchar *
new_vcard_from_test_case (const gchar *case_name)
{
	gchar *filename;
	gchar *case_filename;
	GFile * file;
	GError *error = NULL;
	gchar *vcard;

	case_filename = g_strdup_printf ("%s.vcf", case_name);
	filename = g_build_filename (SRCDIR, "..", "data", "vcards", case_filename, NULL);
	file = g_file_new_for_path (filename);
	if (!g_file_load_contents (file, NULL, &vcard, NULL, NULL, &error)) {
		g_warning (
			"failed to read test contact file '%s': %s",
				filename, error->message);
		exit (1);
	}

	g_free (case_filename);
	g_free (filename);
	g_object_unref (file);

	return vcard;
}

static gboolean
contacts_are_equal_shallow (EContact *a,
                            EContact *b)
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
add_contact_from_test_case_verify (EBookClient *book_client,
                                   const gchar *case_name,
                                   EContact **contact)
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

gboolean
add_contact_verify (EBookClient *book_client,
                    EContact *contact)
{
	EContact *contact_final;
	gchar *uid;
	GError *error = NULL;

	if (!e_book_client_add_contact_sync (book_client, contact, &uid, NULL, &error)) {
		report_error ("add contact sync", &error);
		return FALSE;
	}

	e_contact_set (contact, E_CONTACT_UID, uid);

	if (!e_book_client_get_contact_sync (book_client, uid, &contact_final, NULL, &error)) {
		report_error ("get contact sync", &error);
		g_free (uid);
		return FALSE;
	}

        /* verify the contact was added "successfully" (not thorough) */
	g_assert (contacts_are_equal_shallow (contact, contact_final));

	g_object_unref (contact_final);
	g_free (uid);

	return TRUE;
}
