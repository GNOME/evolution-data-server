/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <locale.h>
#include <libebook/libebook.h>
#include <libedata-book/libedata-book.h>

#include "client-test-utils.h"
#include "e-test-server-utils.h"

static ETestServerClosure book_closure = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0 };

#define N_THREADS  20
#define N_CONTACTS 5

typedef struct {
	GThread         *thread;
	const gchar     *book_uid;
	EBookClient     *client;
	EBookClientView *view;

	GMainLoop       *loop;

	GMutex           complete_mutex;
	GCond            complete_cond;
	gboolean         complete;
	gint             n_contacts;
} ThreadData;

static void
objects_added (EBookClientView *view,
               const GSList *contacts,
	       ThreadData *data)
{
	const GSList *l;

	for (l = contacts; l; l = l->next) {
		/* print_email (l->data); */

		data->n_contacts++;
	}
}

static void
objects_modified (EBookClientView *view,
		  const GSList *contacts,
		  ThreadData *data)
{
	const GSList *l;

	for (l = contacts; l; l = l->next) {
		/* print_email (l->data); */
	}
}

static void
objects_removed (EBookClientView *view,
                 const GSList *ids,
		 ThreadData *data)
{
	const GSList *l;

	for (l = ids; l; l = l->next) {
		/* printf ("   Removed contact: %s\n", (gchar *) l->data); */

		data->n_contacts--;
	}
}

static void
complete (EBookClientView *view,
          const GError *error,
	  ThreadData *data)
{
	g_mutex_lock (&data->complete_mutex);
	data->complete = TRUE;
	g_cond_signal (&data->complete_cond);
	g_mutex_unlock (&data->complete_mutex);
}

static void
view_ready (GObject *source_object,
	    GAsyncResult *result,
	    ThreadData *data)
{
	GError *error = NULL;

	if (!e_book_client_get_view_finish (E_BOOK_CLIENT (source_object), result, &(data->view), &error))
		g_error ("Error getting view: %s", error->message);

	g_signal_connect (data->view, "objects-added", G_CALLBACK (objects_added), data);
	g_signal_connect (data->view, "objects-modified", G_CALLBACK (objects_modified), data);
	g_signal_connect (data->view, "objects-removed", G_CALLBACK (objects_removed), data);
	g_signal_connect (data->view, "complete", G_CALLBACK (complete), data);

	e_book_client_view_set_fields_of_interest (data->view, NULL, &error);
	if (error)
		g_error ("set fields of interest: %s", error->message);

	e_book_client_view_start (data->view, &error);
	if (error)
		g_error ("start view: %s", error->message);
}

static gboolean
start_view (ThreadData *data)
{
	EBookQuery   *query;
	gchar        *sexp;

	query = e_book_query_any_field_contains ("");
	sexp = e_book_query_to_string (query);

	e_book_client_get_view (data->client, sexp, NULL, (GAsyncReadyCallback)view_ready, data);

	e_book_query_unref (query);
	g_free (sexp);

	return FALSE;
}

static void
start_thread_test (ThreadData *data)
{
	GMainContext *context;
	GSource      *source;

	context = g_main_loop_get_context (data->loop);
	source  = g_idle_source_new ();

	g_source_set_callback (source, (GSourceFunc)start_view, data, NULL);
	g_source_attach (source, context);
}

static void
finish_thread_test (ThreadData *data)
{
	g_assert_cmpint (data->n_contacts, ==, N_CONTACTS);

	g_main_loop_quit (data->loop);
	g_thread_join (data->thread);
	g_mutex_clear (&data->complete_mutex);
	g_cond_clear (&data->complete_cond);
	g_slice_free (ThreadData, data);
}

static void
client_ready (GObject *source_object,
	      GAsyncResult *res,
	      ThreadData *data)
{
	GError *error = NULL;

	if (!e_client_open_finish (E_CLIENT (source_object), res, &error))
		g_error ("Error opening client: %s",
			 error->message);

	start_thread_test (data);
}

static gpointer
test_view_thread (ThreadData *data)
{
	GMainContext    *context;
	ESourceRegistry *registry;
	ESource         *source;
	GError          *error = NULL;

	context    = g_main_context_new ();
	data->loop = g_main_loop_new (context, FALSE);
	g_main_context_push_thread_default (context);

	/* Open the test book client in this thread */
	registry = e_source_registry_new_sync (NULL, &error);
	if (!registry)
		g_error ("Unable to create the registry: %s", error->message);

	source = e_source_registry_ref_source (registry, data->book_uid);
	if (!source)
		g_error ("Unable to fetch source uid '%s' from the registry", data->book_uid);

	if (g_getenv ("DEBUG_DIRECT") != NULL)
		data->client = e_book_client_new_direct (registry, source, &error);
	else
		data->client = e_book_client_new (source, &error);

	if (!data->client)
		g_error ("Unable to create EBookClient for uid '%s': %s", data->book_uid, error->message);

	e_client_open (E_CLIENT (data->client), TRUE, NULL, (GAsyncReadyCallback)client_ready, data);

	g_main_loop_run (data->loop);

	g_object_unref (source);
	g_object_unref (registry);

	g_object_unref (data->client);
	g_main_context_pop_thread_default (context);
	g_main_loop_unref (data->loop);
	g_main_context_unref (context);

	return NULL;
}

static ThreadData *
create_test_thread (const gchar   *book_uid)
{
	ThreadData  *data = g_slice_new0 (ThreadData);

	data->book_uid    = book_uid;

	g_mutex_init (&data->complete_mutex);
	g_cond_init (&data->complete_cond);

	data->thread = g_thread_new ("test-thread", (GThreadFunc)test_view_thread, data);

	return data;
}

static void
test_concurrent_views (ETestServerFixture *fixture,
		       gconstpointer       user_data)
{
	EBookClient *main_client;
	ESource *source;
	const gchar *book_uid = NULL;
	ThreadData **tests;
	gint i;

	main_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);
	source = e_client_get_source (E_CLIENT (main_client));
	book_uid = e_source_get_uid (source);

	/* Create out test contacts */
	if (!add_contact_from_test_case_verify (main_client, "custom-1", NULL) ||
	    !add_contact_from_test_case_verify (main_client, "custom-2", NULL) ||
	    !add_contact_from_test_case_verify (main_client, "custom-3", NULL) ||
	    !add_contact_from_test_case_verify (main_client, "custom-4", NULL) ||
	    !add_contact_from_test_case_verify (main_client, "custom-5", NULL)) {
		g_object_unref (main_client);
		g_error ("Failed to create contacts for testing");
	}

	/* Create all concurrent threads accessing the same addressbook */
	tests = g_new0 (ThreadData *, N_THREADS);
	for (i = 0; i < N_THREADS; i++)
		tests[i] = create_test_thread (book_uid);


	/* Wait for all threads to receive the complete signal */
	for (i = 0; i < N_THREADS; i++) {
		g_mutex_lock (&(tests[i]->complete_mutex));
		while (!tests[i]->complete)
			g_cond_wait (&(tests[i]->complete_cond),
				     &(tests[i]->complete_mutex));
		g_mutex_unlock (&(tests[i]->complete_mutex));
	}

	/* Finish all tests */
	for (i = 0; i < N_THREADS; i++)
		finish_thread_test (tests[i]);

	/* Cleanup */
	g_free (tests);
}

gint
main (gint argc,
      gchar **argv)
{
#if !GLIB_CHECK_VERSION (2, 35, 1)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);
	setlocale (LC_ALL, "en_US.UTF-8");

	g_test_add ("/EBookClient/ConcurrentViews", ETestServerFixture, &book_closure,
		    e_test_server_utils_setup, test_concurrent_views, e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}
