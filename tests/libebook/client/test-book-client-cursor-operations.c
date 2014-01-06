/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2013 Intel Corporation
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
 * Authors: Tristan Van Berkom <tristanvb@openismus.com>
 */
#include <config.h>

#include <libebook/libebook.h>

#include "e-test-server-utils.h"
#include "client-test-utils.h"
#include "e-dbus-localed.h"

/* Filter which tests we want to try with a regexp */
static GRegex *test_regex = NULL;
static gchar  *test_filter = NULL;
static GOptionEntry entries[] = {

	{ "filter", 'f', 0, G_OPTION_ARG_STRING, &test_filter,
	  "A regular expression to filter which tests should be added", NULL },
	{ NULL }
};

/* This is based on the sorted-%02d.vcf contacts from 1-20,
 * a full table describing the expected order under various
 * locales can be found in tests/libedata-book/data-test-utils.h
 */
#define N_SORTED_CONTACTS    20

/* 13 contacts in the test data have an email address ending with ".com" */
#define N_FILTERED_CONTACTS  13

/* How long to wait before aborting an async test */
#define TIMEOUT              30 * 1000

/* Standard number of iterations for a parallelized operation */
#define THREAD_ITERATIONS    20

/* Number of threads to use in the concurrent thread tests */
#define CONCURRENT_THREADS   5

typedef struct _CursorClosure    CursorClosure;
typedef struct _CursorFixture    CursorFixture;
typedef enum   _CursorTestType   CursorTestType;
typedef struct _CursorTest       CursorTest;
typedef struct _CursorThread     CursorThread;
typedef struct _CursorThreadTest CursorThreadTest;
typedef struct _CursorParams     CursorParams;

typedef void (*CursorThreadFunc) (CursorFixture    *fixture,
				  CursorClosure    *closure,
				  CursorThread     *thread,
				  gpointer          user_data);

/* Main Cursor Test Fixture */
static void           cursor_fixture_setup            (CursorFixture      *fixture,
						       gconstpointer       user_data);
static void           cursor_fixture_teardown         (CursorFixture      *fixture,
						       gconstpointer       user_data);
static void           cursor_fixture_set_locale       (CursorFixture      *fixture,
						       const gchar        *locale);
static void           cursor_fixture_timeout_start    (CursorFixture      *fixture,
						       const gchar        *error_message);
static void           cursor_fixture_timeout_cancel   (CursorFixture      *fixture);

/* Main Cursor Test Closure */
static CursorClosure *cursor_closure_new              (CursorParams       *params,
						       const gchar        *locale);
static void           cursor_closure_free             (CursorClosure      *closure);
static void           cursor_test_free                (CursorTest         *test);
static void           cursor_closure_add              (CursorClosure      *closure,
						       const gchar        *format,
						       ...) G_GNUC_PRINTF (2, 3);

/* Move By Tests */
static void           cursor_closure_step             (CursorClosure      *closure,
						       EBookCursorStepFlags flags,
						       EBookCursorOrigin   origin,
						       gint                count,
						       gint                expected,
						       ...);
static void           cursor_test_step                (CursorFixture      *fixture,
						       CursorClosure      *closure,
						       CursorTest         *test);
static void           cursor_test_step_free           (CursorTest         *test);

/* Set Sexp Tests */
static void           cursor_closure_set_sexp         (CursorClosure      *closure,
						       EBookQuery         *query);
static void           cursor_test_set_sexp            (CursorFixture      *fixture,
						       CursorClosure      *closure,
						       CursorTest         *test);
static void           cursor_test_set_sexp_free       (CursorTest         *test);

/* Position Tests */
static void           cursor_closure_position         (CursorClosure      *closure,
						       gint                total,
						       gint                position,
						       gboolean            immediate);
static void           cursor_test_position            (CursorFixture      *fixture,
						       CursorClosure      *closure,
						       CursorTest         *test);
static void           cursor_test_position_free       (CursorTest         *test);

/* Add / Remove Tests */
static void           cursor_closure_add_contact      (CursorClosure      *closure,
						       const gchar        *case_name);
static void           cursor_closure_remove_contact   (CursorClosure      *closure,
						       const gchar        *case_name);
static void           cursor_test_add_remove          (CursorFixture      *fixture,
						       CursorClosure      *closure,
						       CursorTest         *test);
static void           cursor_test_add_remove_free     (CursorTest         *test);

/* Alphabet Index Tests */
static void           cursor_closure_alphabet_index   (CursorClosure      *closure,
						       gint                index);
static void           cursor_test_alphabet_index      (CursorFixture      *fixture,
						       CursorClosure      *closure,
						       CursorTest         *test);
static void           cursor_test_alphabet_index_free (CursorTest         *test);

/* Change Locale Tests */
static void           cursor_closure_change_locale    (CursorClosure      *closure,
						       const gchar        *locale);
static void           cursor_test_change_locale       (CursorFixture      *fixture,
						       CursorClosure      *closure,
						       CursorTest         *test);
static void           cursor_test_change_locale_free  (CursorTest         *test);

/* Alphabet Tests */
static void           cursor_closure_alphabet         (CursorClosure      *closure,
						       gboolean            immediate,
						       const gchar        *letter0,
						       const gchar        *letter1,
						       const gchar        *letter2,
						       const gchar        *letter3,
						       const gchar        *letter4);
static void           cursor_test_alphabet            (CursorFixture      *fixture,
						       CursorClosure      *closure,
						       CursorTest         *test);
static void           cursor_test_alphabet_free       (CursorTest         *test);

/* Thread Tests */
static CursorThread  *cursor_closure_thread           (CursorClosure      *closure,
						       gboolean            create_cursor,
						       const gchar        *format,
						       ...) G_GNUC_PRINTF (3, 4);
static void           cursor_test_thread              (CursorFixture      *fixture,
						       CursorClosure      *closure,
						       CursorTest         *test);
static void           cursor_test_thread_free         (CursorTest         *test);
static void           cursor_thread_add_test          (CursorThread       *thread,
						       CursorThreadFunc    test_func,
						       gpointer            data,
						       GDestroyNotify      destroy_data);

/* Thread Join Tests */
static void           cursor_closure_thread_join      (CursorClosure      *closure,
						       CursorThread       *thread);
static void           cursor_test_thread_join         (CursorFixture      *fixture,
						       CursorClosure      *closure,
						       CursorTest         *test);
static void           cursor_test_thread_join_free    (CursorTest         *test);

/* Spinning the main loop */
static void           cursor_closure_spin_loop        (CursorClosure      *closure,
						       guint               ms);
static void           cursor_test_spin_loop           (CursorFixture      *fixture,
						       CursorClosure      *closure,
						       CursorTest         *test);
static void           cursor_test_spin_loop_free      (CursorTest         *test);

struct _CursorFixture {
	ETestServerFixture parent_fixture;

	EBookClientCursor *cursor;
	EContact          *contacts[N_SORTED_CONTACTS];
	EBookQuery        *query;
	GSource           *timeout_source;

	EDBusLocale1      *locale1;
	guint              own_id;
};

struct _CursorClosure {
	ETestServerClosure   parent_closure;
	gchar               *locale;
	gboolean             async;
	GList               *tests;
};

enum _CursorTestType {
	CURSOR_TEST_STEP,
	CURSOR_TEST_SET_SEXP,
	CURSOR_TEST_POSITION,
	CURSOR_TEST_ADD_REMOVE,
	CURSOR_TEST_ALPHABET_INDEX,
	CURSOR_TEST_CHANGE_LOCALE,
	CURSOR_TEST_ALPHABET,
	CURSOR_TEST_THREAD,
	CURSOR_TEST_THREAD_JOIN,
	CURSOR_TEST_SPIN_LOOP
};

struct _CursorTest {
	CursorTestType type;
};

/******************************************************
 *                Main Cursor Test Fixture            *
 ******************************************************/
#define N_SORT_FIELDS 2
static EContactField sort_fields[] = { E_CONTACT_FAMILY_NAME, E_CONTACT_GIVEN_NAME };
static EBookCursorSortType sort_types[] = { E_BOOK_CURSOR_SORT_ASCENDING, E_BOOK_CURSOR_SORT_ASCENDING };

struct _CursorParams {
	const gchar *base_path;
	gboolean     async;
	gboolean     dra;
	gboolean     empty_summary;
};

static gboolean
cursor_timeout (const gchar *error_message)
{
	g_error ("%s", error_message);

	return FALSE;
}

static void
timeout_start (GMainContext *context,
               GSource **source,
               const gchar *error_message)
{
	g_assert (source && *source == NULL);

	if (!context)
		context = g_main_context_default ();

	*source = g_timeout_source_new (TIMEOUT);
	g_source_set_callback (
		*source, (GSourceFunc) cursor_timeout,
		g_strdup (error_message),
		(GDestroyNotify) g_free);
	g_source_attach (*source, context);
}

static void
timeout_cancel (GSource **source)
{
	g_assert (source && *source != NULL);

	g_source_destroy (*source);
	g_source_unref (*source);
	*source = NULL;
}

static void
cursor_fixture_timeout_start (CursorFixture *fixture,
                              const gchar *error_message)
{
	timeout_start (NULL, &fixture->timeout_source, error_message);
}

static void
cursor_fixture_timeout_cancel (CursorFixture *fixture)
{
	timeout_cancel (&fixture->timeout_source);
}

static void
cursor_fixture_add_contacts (CursorFixture *fixture,
                             ETestServerClosure *closure)
{
	EBookClient *book_client;
	GError      *error = NULL;
	GSList      *contacts = NULL;
	gint         i;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	for (i = 0; i < N_SORTED_CONTACTS; i++) {
		gchar *case_name = g_strdup_printf ("sorted-%d", i + 1);
		gchar *vcard;
		EContact *contact;

		vcard = new_vcard_from_test_case (case_name);
		contact = e_contact_new_from_vcard (vcard);
		contacts = g_slist_prepend (contacts, contact);
		g_free (vcard);
		g_free (case_name);

		fixture->contacts[i] = contact;
	}

	if (!e_book_client_add_contacts_sync (book_client, contacts, NULL, NULL, &error)) {

		/* Dont complain here, we re-use the same addressbook for multiple tests
		 * and we can't add the same contacts twice
		 */
		if (g_error_matches (error, E_BOOK_CLIENT_ERROR,
				     E_BOOK_CLIENT_ERROR_CONTACT_ID_ALREADY_EXISTS))
			g_clear_error (&error);
		else
			g_error ("Failed to add test contacts: %s", error->message);
	}

	g_slist_free (contacts);
}

static void
cursor_ready_cb (GObject *source_object,
                 GAsyncResult *result,
                 gpointer user_data)
{
	CursorFixture *fixture = (CursorFixture *) user_data;
	ETestServerFixture *server_fixture = (ETestServerFixture *) fixture;
	GError *error = NULL;

	if (!e_book_client_get_cursor_finish (E_BOOK_CLIENT (source_object),
					      result, &(fixture->cursor), &error))
		g_error ("Failed to create a cursor: %s", error->message);

	g_main_loop_quit (server_fixture->loop);
}

static void
cursor_fixture_setup (CursorFixture *fixture,
                      gconstpointer user_data)
{
	CursorClosure      *closure = (CursorClosure *) user_data;
	ETestServerClosure *server_closure = (ETestServerClosure *) user_data;
	ETestServerFixture *server_fixture = (ETestServerFixture *) fixture;
	EBookClient        *book_client;
	GError             *error = NULL;
	gchar              *sexp = NULL;

	e_test_server_utils_setup (server_fixture, user_data);

	/* Set the initial locale before adding the contacts */
	if (closure->locale)
		cursor_fixture_set_locale (fixture, closure->locale);
	else
		cursor_fixture_set_locale (fixture, "en_US.UTF-8");

	cursor_fixture_add_contacts (fixture, server_closure);

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	/* Allow a surrounding fixture setup
	 * to add a query here
	 */
	if (fixture->query) {
		sexp = e_book_query_to_string (fixture->query);
		e_book_query_unref (fixture->query);
		fixture->query = NULL;
	}

	if (server_closure->use_async_connect) {

		e_book_client_get_cursor (
			book_client,
			sexp,
			sort_fields,
			sort_types,
			N_SORT_FIELDS,
			NULL,
			cursor_ready_cb,
			fixture);

		cursor_fixture_timeout_start (fixture, "Timeout reached while trying to create a cursor");
		g_main_loop_run (server_fixture->loop);
		cursor_fixture_timeout_cancel (fixture);

	} else {

		if (!e_book_client_get_cursor_sync (book_client,
						    sexp,
						    sort_fields,
						    sort_types,
						    N_SORT_FIELDS,
						    &fixture->cursor,
						    NULL, &error))
			g_error ("Failed to create a cursor with an empty query: %s", error->message);
	}

	g_free (sexp);
}

static void
cursor_fixture_teardown (CursorFixture *fixture,
                         gconstpointer user_data)
{
	ETestServerFixture *server_fixture = (ETestServerFixture *) fixture;
	gint i;

	g_object_unref (fixture->cursor);
	for (i = 0; i < N_SORTED_CONTACTS; i++) {
		if (fixture->contacts[i])
			g_clear_object (&(fixture->contacts[i]));
	}

	if (fixture->locale1)
		g_object_unref (fixture->locale1);

	if (fixture->own_id > 0)
		g_bus_unown_name (fixture->own_id);

	e_test_server_utils_teardown (server_fixture, user_data);
}

typedef struct {
	CursorFixture *fixture;
	const gchar *locale;
} ChangeLocaleData;

static void
book_client_locale_change (EBookClient *book,
                           GParamSpec *pspec,
                           ChangeLocaleData *data)
{
	ETestServerFixture *base_fixture = (ETestServerFixture *) data->fixture;

	if (!g_strcmp0 (e_book_client_get_locale (book), data->locale))
		g_main_loop_quit (base_fixture->loop);
}

static void
cursor_fixture_set_locale (CursorFixture *fixture,
                           const gchar *locale)
{
	ETestServerFixture *server_fixture = (ETestServerFixture *) fixture;
	EBookClient *book_client;
	ChangeLocaleData data = { fixture, locale };
	gulong handler_id;
	gchar *strv[2] = { NULL, NULL };

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	/* We're already in the right locale */
	if (g_strcmp0 (locale, e_book_client_get_locale (book_client)) == 0)
		return;

	if (!fixture->locale1) {
		GDBusConnection *bus;
		GError *error = NULL;

		/* We use the 'org.freedesktop.locale1 on the session bus instead
		 * of the system bus only for testing purposes... in real life
		 * this service is on the system bus.
		 */
		bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
		if (!bus)
			g_error ("Failed to get system bus: %s", error->message);

		fixture->locale1 = e_dbus_locale1_skeleton_new ();

		/* Set initial locale before exporting on the bus */
		strv[0] = g_strdup_printf ("LANG=%s", locale);
		e_dbus_locale1_set_locale (fixture->locale1, (const gchar * const *) strv);
		g_free (strv[0]);

		if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (fixture->locale1),
						       bus, "/org/freedesktop/locale1", &error))
			g_error ("Failed to export org.freedesktop.locale1: %s", error->message);

		fixture->own_id =
			g_bus_own_name_on_connection (
				bus,
				"org.freedesktop.locale1",
				G_BUS_NAME_OWNER_FLAGS_REPLACE,
				NULL, NULL, NULL, NULL);

		g_object_unref (bus);
	} else {
		/* Send locale change message */
		strv[0] = g_strdup_printf ("LANG=%s", locale);
		e_dbus_locale1_set_locale (fixture->locale1, (const gchar * const *) strv);
		g_free (strv[0]);
	}

	handler_id = g_signal_connect (
		book_client, "notify::locale",
		G_CALLBACK (book_client_locale_change), &data);

	cursor_fixture_timeout_start (fixture, "Timeout reached while waiting for locale change");
	g_main_loop_run (server_fixture->loop);
	cursor_fixture_timeout_cancel (fixture);

	g_signal_handler_disconnect (book_client, handler_id);
}

static void
cursor_fixture_test (CursorFixture *fixture,
                     gconstpointer user_data)
{
	CursorClosure *closure = (CursorClosure *) user_data;
	GList         *l;

	/* Run all the tests */
	for (l = closure->tests; l; l = l->next) {
		CursorTest *test = l->data;

		switch (test->type) {
		case CURSOR_TEST_STEP:
			cursor_test_step (fixture, closure, test);
			break;
		case CURSOR_TEST_SET_SEXP:
			cursor_test_set_sexp (fixture, closure, test);
			break;
		case CURSOR_TEST_POSITION:
			cursor_test_position (fixture, closure, test);
			break;
		case CURSOR_TEST_ADD_REMOVE:
			cursor_test_add_remove (fixture, closure, test);
			break;
		case CURSOR_TEST_ALPHABET_INDEX:
			cursor_test_alphabet_index (fixture, closure, test);
			break;
		case CURSOR_TEST_CHANGE_LOCALE:
			cursor_test_change_locale (fixture, closure, test);
			break;
		case CURSOR_TEST_ALPHABET:
			cursor_test_alphabet (fixture, closure, test);
			break;
		case CURSOR_TEST_THREAD:
			cursor_test_thread (fixture, closure, test);
			break;
		case CURSOR_TEST_THREAD_JOIN:
			cursor_test_thread_join (fixture, closure, test);
			break;
		case CURSOR_TEST_SPIN_LOOP:
			cursor_test_spin_loop (fixture, closure, test);
			break;
		}
	}
}

/******************************************************
 *                Main Cursor Test Closure            *
 ******************************************************/
static void
cursor_test_free (CursorTest *test)
{
	switch (test->type) {
	case CURSOR_TEST_STEP:
		cursor_test_step_free (test);
		break;
	case CURSOR_TEST_SET_SEXP:
		cursor_test_set_sexp_free (test);
		break;
	case CURSOR_TEST_POSITION:
		cursor_test_position_free (test);
		break;
	case CURSOR_TEST_ADD_REMOVE:
		cursor_test_add_remove_free (test);
		break;
	case CURSOR_TEST_ALPHABET_INDEX:
		cursor_test_alphabet_index_free (test);
		break;
	case CURSOR_TEST_CHANGE_LOCALE:
		cursor_test_change_locale_free (test);
		break;
	case CURSOR_TEST_ALPHABET:
		cursor_test_alphabet_free (test);
		break;
	case CURSOR_TEST_THREAD:
		cursor_test_thread_free (test);
		break;
	case CURSOR_TEST_THREAD_JOIN:
		cursor_test_thread_join_free (test);
		break;
	case CURSOR_TEST_SPIN_LOOP:
		cursor_test_spin_loop_free (test);
		break;
	}
}

static void
cursor_closure_free (CursorClosure *closure)
{
	if (closure) {
		g_list_free_full (closure->tests, (GDestroyNotify) cursor_test_free);
		g_free (closure->locale);
		g_slice_free (CursorClosure, closure);
	}
}

static void
cursor_closure_setup_empty_summary (ESource *scratch,
                                    ETestServerClosure *closure)
{
	ESourceBackendSummarySetup *setup;

	/* Setup the book for fallback mode, make sure the
	 * summary is empty to provoke the backend into enacting the fallback routines
	 */
	g_type_ensure (E_TYPE_SOURCE_BACKEND_SUMMARY_SETUP);
	setup = e_source_get_extension (scratch, E_SOURCE_EXTENSION_BACKEND_SUMMARY_SETUP);
	e_source_backend_summary_setup_set_summary_fields (
		setup,
		/* Just one boolean field */
		E_CONTACT_WANTS_HTML,
		0);
}

static CursorClosure *
cursor_closure_new (CursorParams *params,
                    const gchar *locale)
{
	CursorClosure *closure = g_slice_new0 (CursorClosure);
	ETestServerClosure *server_closure = (ETestServerClosure *) closure;

	if (params->dra)
		server_closure->type = E_TEST_SERVER_DIRECT_ADDRESS_BOOK;
	else
		server_closure->type = E_TEST_SERVER_ADDRESS_BOOK;

	if (params->empty_summary)
		server_closure->customize = cursor_closure_setup_empty_summary;

	server_closure->use_async_connect = params->async;
	server_closure->destroy_closure_func = (GDestroyNotify) cursor_closure_free;

	closure->locale = g_strdup (locale);
	closure->async = params->async;

	return closure;
}

static void
cursor_closure_add (CursorClosure *closure,
                    const gchar *format,
                    ...)
{
	gchar *test_path;
	va_list args;

	va_start (args, format);
	test_path = g_strdup_vprintf (format, args);
	va_end (args);

	/* Filter out anything that was not specified in the test filter */
	if (test_regex == NULL ||
	    g_regex_match (test_regex, test_path, 0, NULL))
		g_test_add (
			test_path, CursorFixture, closure,
			cursor_fixture_setup,
			cursor_fixture_test,
			cursor_fixture_teardown);

	g_free (test_path);
}

/******************************************************
 *                      Step Tests                    *
 ******************************************************/
typedef struct {
	CursorTestType    type;

	EBookCursorStepFlags flags;   /* The flags for this step */
	EBookCursorOrigin origin;   /* The origin to step from */
	gint              count;    /* The amount of contacts to step the cursor by (positive or negative) */
	gint              expected; /* The amount of actual results expected */
	gint              expected_order[N_SORTED_CONTACTS]; /* The expected results */
} CursorTestStep;

typedef struct {
	CursorFixture    *fixture;
	CursorTestStep   *test;
	gboolean          out_of_sync;
} StepReadyData;

static gint
find_contact_link (EContact *contact,
                   const gchar *uid)
{
	const gchar *contact_uid =
		(const gchar *) e_contact_get_const (contact, E_CONTACT_UID);

	return g_strcmp0 (contact_uid, uid);
}

static void
assert_contacts_order_slist (GSList *results,
                             GSList *uids)
{
	gint position = -1;
	GSList *link, *l;

	/* Assert that all passed UIDs are found in the
	 * results, and that those UIDs are in the
	 * specified order.
	 */
	for (l = uids; l; l = l->next) {
		const gchar *uid = l->data;
		gint new_position;

		link = g_slist_find_custom (results, uid, (GCompareFunc) find_contact_link);
		if (!link)
			g_error ("Specified uid '%s' was not found in results", uid);

		new_position = g_slist_position (results, link);
		g_assert_cmpint (new_position, >, position);
		position = new_position;
	}
}

static void
step_print_results (GSList *results,
                    GSList *uids)
{
	GSList *l;

	if (g_getenv ("TEST_DEBUG") == NULL)
		return;

	g_print ("\nPRINTING EXPECTED RESULTS:\n");
	for (l = uids; l; l = l->next) {
		gchar *uid = l->data;

		g_print ("\t%s\n", uid);
	}
	g_print ("\nEXPECTED RESULTS FINISHED\n");

	g_print ("\nPRINTING RESULTS:\n");
	for (l = results; l; l = l->next) {
		EContact *contact = l->data;
		gchar    *vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);

		g_print ("\n%s\n", vcard);
		g_free (vcard);
	}
	g_print ("\nRESULT LIST FINISHED\n");
}

static void
cursor_test_assert_results (CursorFixture *fixture,
                            CursorTestStep *test,
                            GSList *results,
                            gint n_reported_results)
{
	GSList *uids = NULL;
	gint    i;

	for (i = 0; i < test->expected; i++) {
		gchar *uid;
		gint   index = test->expected_order[i];

		uid = (gchar *) e_contact_get_const (fixture->contacts[index], E_CONTACT_UID);
		uids = g_slist_append (uids, uid);
	}

	step_print_results (results, uids);

	/* Assert the exact amount of requested results */
	g_assert_cmpint (g_slist_length (results), ==, test->expected);
	g_assert_cmpint (n_reported_results, ==, test->expected);
	assert_contacts_order_slist (results, uids);
	g_slist_free (uids);
}

static void
cursor_test_step_free (CursorTest *test)
{
	CursorTestStep *step = (CursorTestStep *) test;

	g_slice_free (CursorTestStep, step);
}

static void
cursor_test_step_ready_cb (GObject *source_object,
                           GAsyncResult *result,
                           gpointer user_data)
{
	StepReadyData      *data = (StepReadyData *) user_data;
	ETestServerFixture *server_fixture = (ETestServerFixture *) data->fixture;
	GSList             *results = NULL;
	gint                n_reported_results;
	gboolean            end_of_list = FALSE;
	GError             *error = NULL;

	n_reported_results =
		e_book_client_cursor_step_finish (
			E_BOOK_CLIENT_CURSOR (source_object),
			result, &results, &error);

	if (n_reported_results < 0) {
		if (g_error_matches (error,
				     E_CLIENT_ERROR,
				     E_CLIENT_ERROR_OUT_OF_SYNC)) {
			data->out_of_sync = TRUE;
			g_clear_error (&error);
		} else if (g_error_matches (error,
					    E_CLIENT_ERROR,
					    E_CLIENT_ERROR_QUERY_REFUSED)) {
			end_of_list = TRUE;
			g_clear_error (&error);
		} else {
			g_error (
				"Error calling e_book_client_cursor_step_finish(): %s",
				error->message);
		}
	}

	if (!data->out_of_sync) {

		if (data->test->expected < 0) {
			g_assert_cmpint (n_reported_results, <, 0);
			g_assert_cmpint (end_of_list, ==, TRUE);
		} else
			cursor_test_assert_results (data->fixture, data->test, results, n_reported_results);
	}

	g_slist_free_full (results, (GDestroyNotify) g_object_unref);
	g_main_loop_quit (server_fixture->loop);
}

static gboolean
cursor_test_try_step (CursorFixture *fixture,
                      CursorClosure *closure,
                      CursorTest *test)
{
	ETestServerFixture *server_fixture = (ETestServerFixture *) fixture;
	CursorTestStep     *step = (CursorTestStep *) test;
	gboolean            out_of_sync = FALSE;
	gboolean            end_of_list = FALSE;

	if (closure->async) {
		StepReadyData data = { fixture, step, FALSE };

		e_book_client_cursor_step (
			fixture->cursor,
			step->flags,
			step->origin,
			step->count,
			NULL,
			cursor_test_step_ready_cb,
			&data);

		/* Wait for result with an error timeout */
		cursor_fixture_timeout_start (fixture, "Timeout reached while moving the cursor");
		g_main_loop_run (server_fixture->loop);
		cursor_fixture_timeout_cancel (fixture);

	} else {
		GError *error = NULL;
		GSList *results = NULL;
		gint    n_reported_results;

		n_reported_results =
			e_book_client_cursor_step_sync (
				fixture->cursor,
							step->flags,
							step->origin,
							step->count,
							&results,
							NULL, &error);

		if (n_reported_results < 0) {
			if (g_error_matches (error,
					     E_CLIENT_ERROR,
					     E_CLIENT_ERROR_OUT_OF_SYNC)) {
				out_of_sync = TRUE;
				g_clear_error (&error);
			} else if (g_error_matches (error,
						    E_CLIENT_ERROR,
						    E_CLIENT_ERROR_QUERY_REFUSED)) {
				end_of_list = TRUE;
				g_clear_error (&error);
			} else {
				g_error (
					"Error calling e_book_client_cursor_step_sync(): %s",
					error->message);
			}
		}

		if (step->expected < 0) {
			g_assert_cmpint (n_reported_results, <, 0);
			g_assert_cmpint (end_of_list, ==, TRUE);
		} else
			cursor_test_assert_results (fixture, step, results, n_reported_results);

		g_slist_free_full (results, g_object_unref);
	}

	return out_of_sync == FALSE;
}

static void
step_refreshed (EBookClientCursor *cursor,
                GMainLoop *loop)
{
	g_main_loop_quit (loop);
}

static void
cursor_test_step (CursorFixture *fixture,
                  CursorClosure *closure,
                  CursorTest *test)
{
	ETestServerFixture *server_fixture = (ETestServerFixture *) fixture;
	gint                i;
	gboolean            success = FALSE;

	/* Should only ever really be once, but in practice code
	 * should be written to continually fallback until a synchronized
	 * step() call passes */
	for (i = 0; i < 3 && success == FALSE; i++) {
		gulong refresh_id;

		/* Wait for a refresh which follows an out-of-sync move by */
		refresh_id = g_signal_connect (
			fixture->cursor, "refresh",
			G_CALLBACK (step_refreshed),
			server_fixture->loop);

		/* Stop trying after the first step() is not out-of-sync */
		success = cursor_test_try_step (fixture, closure, test);

		if (!success) {
			cursor_fixture_timeout_start (fixture, "Timeout reached while waiting for refresh event");
			g_main_loop_run (server_fixture->loop);
			cursor_fixture_timeout_cancel (fixture);
		}

		g_signal_handler_disconnect (fixture->cursor, refresh_id);
	}
}

/* Expected is the number of expected results,
 * the var args are the result indexes as
 * listed in data-test-utils.h
 *
 * If expected is -1, then E_CLIENT_ERROR_QUERY_REFUSED
 * is expected to be triggered by this step.
 */
static void
cursor_closure_step (CursorClosure *closure,
                     EBookCursorStepFlags flags,
                     EBookCursorOrigin origin,
                     gint count,
                     gint expected,
                     ...)
{
	CursorTestStep *test = g_slice_new0 (CursorTestStep);
	va_list args;
	gint i;

	g_assert (expected <= N_SORTED_CONTACTS);
	g_assert (ABS (count) <= N_SORTED_CONTACTS);

	test->type = CURSOR_TEST_STEP;
	test->flags = flags;
	test->origin = origin;
	test->count = count;
	test->expected = expected;

	va_start (args, expected);
	for (i = 0; i < expected; i++) {
		gint expected_position = va_arg (args, gint);

		/* Sanity check while building the test case */
		g_assert_cmpint (expected_position, >, 0);

		test->expected_order[i] = expected_position - 1;
	}
	va_end (args);

	closure->tests = g_list_append (closure->tests, test);
}

/******************************************************
 *                     Set Sexp Tests                 *
 ******************************************************/
typedef struct {
	CursorTestType    type;

	gchar            *sexp;
} CursorTestSetSexp;

typedef struct {
	CursorFixture     *fixture;
	CursorTestSetSexp *test;
} SetSexpReadyData;

static void
cursor_closure_set_sexp (CursorClosure *closure,
                         EBookQuery *query)
{
	CursorTestSetSexp *test = g_slice_new0 (CursorTestSetSexp);

	g_assert (query != NULL);

	test->type = CURSOR_TEST_SET_SEXP;
	test->sexp = e_book_query_to_string (query);
	e_book_query_unref (query);

	closure->tests = g_list_append (closure->tests, test);
}

static void
cursor_test_set_sexp_assert (CursorTestSetSexp *test,
                             gboolean success,
                             GError *error)
{
	if (!success)
		g_error (
			"Failed to set sexp '%s': %s",
			test->sexp, error->message);
}

static void
cursor_test_set_sexp_ready_cb (GObject *source_object,
                               GAsyncResult *result,
                               gpointer user_data)
{
	SetSexpReadyData   *data = (SetSexpReadyData *) user_data;
	ETestServerFixture *server_fixture = (ETestServerFixture *) data->fixture;
	gboolean            success;
	GError             *error = NULL;

	success = e_book_client_cursor_set_sexp_finish (
		E_BOOK_CLIENT_CURSOR (source_object),
							result, &error);
	cursor_test_set_sexp_assert (data->test, success, error);
	g_clear_error (&error);

	g_main_loop_quit (server_fixture->loop);
}

static void
cursor_test_set_sexp (CursorFixture *fixture,
                      CursorClosure *closure,
                      CursorTest *test)
{
	ETestServerFixture *server_fixture = (ETestServerFixture *) fixture;
	CursorTestSetSexp  *set_sexp = (CursorTestSetSexp *) test;

	if (closure->async) {
		SetSexpReadyData data = { fixture, set_sexp };

		e_book_client_cursor_set_sexp (
			fixture->cursor,
			set_sexp->sexp,
			NULL,
			cursor_test_set_sexp_ready_cb,
			&data);

		/* Wait for result with an error timeout */
		cursor_fixture_timeout_start (fixture, "Timeout reached while setting search expression");
		g_main_loop_run (server_fixture->loop);
		cursor_fixture_timeout_cancel (fixture);
	} else {
		gboolean  success;
		GError   *error = NULL;

		success = e_book_client_cursor_set_sexp_sync (
			fixture->cursor,
			set_sexp->sexp,
			NULL,
			&error);
		cursor_test_set_sexp_assert (set_sexp, success, error);
		g_clear_error (&error);
	}
}

static void
cursor_test_set_sexp_free (CursorTest *test)
{
	CursorTestSetSexp *set_sexp = (CursorTestSetSexp *) test;

	g_free (set_sexp->sexp);
	g_slice_free (CursorTestSetSexp, set_sexp);
}

/******************************************************
 *                   Position Tests                   *
 ******************************************************/
typedef struct {
	CursorTestType    type;

	gint              total;
	gint              position;
	gboolean          immediate;

	gulong            total_id;
	gulong            position_id;
} CursorTestPosition;

typedef struct {
	CursorFixture      *fixture;
	CursorTestPosition *test;
} PositionData;

static void
cursor_closure_position (CursorClosure *closure,
                         gint total,
                         gint position,
                         gboolean immediate)
{
	CursorTestPosition *test = g_slice_new0 (CursorTestPosition);

	test->type = CURSOR_TEST_POSITION;
	test->total = total;
	test->position = position;
	test->immediate = immediate;

	closure->tests = g_list_append (closure->tests, test);
}

static void
position_or_total_changed (EBookClientCursor *cursor,
                           GParamSpec *pspec,
                           PositionData *data)
{
	ETestServerFixture *server_fixture = (ETestServerFixture *) data->fixture;

	if (g_getenv ("TEST_DEBUG") != NULL) {
		g_print (
			"Position changed, total: %d position: %d (expecting total: %d position: %d)\n",
			e_book_client_cursor_get_total (cursor),
			e_book_client_cursor_get_position (cursor),
			data->test->total,
			data->test->position);
	}

	if (data->test->total == e_book_client_cursor_get_total (cursor) &&
	    data->test->position == e_book_client_cursor_get_position (cursor))
		g_main_loop_quit (server_fixture->loop);
}

static void
cursor_test_position (CursorFixture *fixture,
                      CursorClosure *closure,
                      CursorTest *test)
{
	ETestServerFixture *server_fixture = (ETestServerFixture *) fixture;
	CursorTestPosition *position = (CursorTestPosition *) test;
	PositionData        data = { fixture, position };

	if (g_getenv ("TEST_DEBUG") != NULL) {
		g_print (
			"Actual total: %d position: %d, %s for total: %d position: %d\n",
			e_book_client_cursor_get_total (fixture->cursor),
			e_book_client_cursor_get_position (fixture->cursor),
			position->immediate ? "Checking" : "Waiting",
			position->total,
			position->position);
	}

	/* If testing immediate mode, just assert the immediate position and don't wait for asynchronous updates*/
	if (position->immediate) {
		g_assert_cmpint (e_book_client_cursor_get_total (fixture->cursor), ==, position->total);
		g_assert_cmpint (e_book_client_cursor_get_position (fixture->cursor), ==, position->position);
	}

	/* Position is already correct */
	if (position->total == e_book_client_cursor_get_total (fixture->cursor) &&
	    position->position == e_book_client_cursor_get_position (fixture->cursor))
		return;

	/* Position & Total is notified asynchronously, connect to signals and
	 * timeout error if the correct position / total is never reached.
	 */
	position->total_id = g_signal_connect (
		fixture->cursor, "notify::total",
		G_CALLBACK (position_or_total_changed),
		&data);
	position->position_id = g_signal_connect (
		fixture->cursor, "notify::position",
		G_CALLBACK (position_or_total_changed),
		&data);

	cursor_fixture_timeout_start (fixture, "Timeout waiting for expected position and total");
	g_main_loop_run (server_fixture->loop);
	cursor_fixture_timeout_cancel (fixture);

	g_signal_handler_disconnect (fixture->cursor, position->total_id);
	g_signal_handler_disconnect (fixture->cursor, position->position_id);
}

static void
cursor_test_position_free (CursorTest *test)
{
	CursorTestPosition *position = (CursorTestPosition *) test;

	g_slice_free (CursorTestPosition, position);
}

/******************************************************
 *                  Add / Remove Tests                *
 ******************************************************/
typedef struct {
	CursorTestType    type;

	gchar            *case_name;
	gboolean          add;
} CursorTestAddRemove;

typedef struct {
	CursorFixture       *fixture;
	CursorTestAddRemove *test;
	EContact            *contact;
} AddRemoveReadyData;

static void
cursor_closure_add_remove_contact (CursorClosure *closure,
                                   const gchar *case_name,
                                   gboolean add)
{
	CursorTestAddRemove *test = g_slice_new0 (CursorTestAddRemove);

	test->type = CURSOR_TEST_ADD_REMOVE;
	test->case_name = g_strdup (case_name);
	test->add = add;

	closure->tests = g_list_append (closure->tests, test);
}

static void
cursor_closure_add_contact (CursorClosure *closure,
                            const gchar *case_name)
{
	cursor_closure_add_remove_contact (closure, case_name, TRUE);
}

static void
cursor_closure_remove_contact (CursorClosure *closure,
                               const gchar *case_name)
{
	cursor_closure_add_remove_contact (closure, case_name, FALSE);
}

static void
cursor_test_add_remove_ready_cb (GObject *source_object,
                                 GAsyncResult *result,
                                 gpointer user_data)
{

	AddRemoveReadyData *data = (AddRemoveReadyData *) user_data;
	ETestServerFixture *server_fixture = (ETestServerFixture *) data->fixture;
	gchar              *new_uid = NULL;
	const gchar        *contact_uid;
	gboolean            success;
	GError             *error = NULL;

	if (data->test->add) {
		success = e_book_client_add_contact_finish (
			E_BOOK_CLIENT (source_object),
			result, &new_uid, &error);

		if (!success)
			g_error ("Error adding contact: %s", error->message);
	} else {
		success = e_book_client_remove_contact_finish (
			E_BOOK_CLIENT (source_object),
			result, &error);

		if (!success)
			g_error ("Error adding contact: %s", error->message);
	}

	g_clear_error (&error);

	if (data->test->add) {
		contact_uid = e_contact_get_const (data->contact, E_CONTACT_UID);
		if (contact_uid)
			g_assert_cmpstr (contact_uid, ==, new_uid);
	}

	g_free (new_uid);

	g_main_loop_quit (server_fixture->loop);
}

static void
cursor_test_add_remove (CursorFixture *fixture,
                        CursorClosure *closure,
                        CursorTest *test)
{
	ETestServerFixture    *server_fixture = (ETestServerFixture *) fixture;
	CursorTestAddRemove   *modify = (CursorTestAddRemove *) test;
	EBookClient           *book_client;
	EContact              *contact;
	gchar                 *vcard;

	vcard = new_vcard_from_test_case (modify->case_name);
	contact = e_contact_new_from_vcard (vcard);
	g_free (vcard);

	book_client = E_TEST_SERVER_UTILS_SERVICE (server_fixture, EBookClient);

	if (closure->async) {
		AddRemoveReadyData data = { fixture, modify, contact };

		if (modify->add)
			e_book_client_add_contact (
				book_client, contact,
				NULL, cursor_test_add_remove_ready_cb, &data);
		else
			e_book_client_remove_contact (
				book_client, contact,
				NULL, cursor_test_add_remove_ready_cb, &data);

		/* Wait for result with an error timeout */
		cursor_fixture_timeout_start (fixture, "Timeout reached while adding a contact");
		g_main_loop_run (server_fixture->loop);
		cursor_fixture_timeout_cancel (fixture);
	} else {
		gboolean  success;
		GError   *error = NULL;
		gchar    *new_uid = NULL;
		const gchar *contact_uid;

		if (modify->add)
			success = e_book_client_add_contact_sync (
				book_client,
				contact,
				&new_uid,
				NULL, &error);
		else
			success = e_book_client_remove_contact_sync (
				book_client,
				contact,
				NULL, &error);

		if (!success)
			g_error ("Error adding contact: %s", error->message);
		g_clear_error (&error);

		if (modify->add) {
			contact_uid = e_contact_get_const (contact, E_CONTACT_UID);
			if (contact_uid)
				g_assert_cmpstr (contact_uid, ==, new_uid);
		}

		g_free (new_uid);
	}

	g_object_unref (contact);
}

static void
cursor_test_add_remove_free (CursorTest *test)
{
	CursorTestAddRemove   *modify = (CursorTestAddRemove *) test;

	g_free (modify->case_name);
	g_slice_free (CursorTestAddRemove, modify);
}

/******************************************************
 *                Alphabet Index Tests                *
 ******************************************************/
typedef struct {
	CursorTestType    type;

	gint              index;
} CursorTestAlphabetIndex;

typedef struct {
	CursorFixture           *fixture;
	CursorTestAlphabetIndex *test;
} AlphabetIndexReadyData;

static void
cursor_closure_alphabet_index (CursorClosure *closure,
                               gint index)
{
	CursorTestAlphabetIndex *test = g_slice_new0 (CursorTestAlphabetIndex);

	test->type = CURSOR_TEST_ALPHABET_INDEX;
	test->index = index;

	closure->tests = g_list_append (closure->tests, test);
}

static void
cursor_test_alphabet_index_ready_cb (GObject *source_object,
                                     GAsyncResult *result,
                                     gpointer user_data)
{

	AlphabetIndexReadyData *data = (AlphabetIndexReadyData *) user_data;
	ETestServerFixture     *server_fixture = (ETestServerFixture *) data->fixture;
	GError                 *error = NULL;

	if (!e_book_client_cursor_set_alphabetic_index_finish (E_BOOK_CLIENT_CURSOR (source_object),
							       result, &error))
		g_error ("Error setting alphabet index: %s", error->message);

	g_main_loop_quit (server_fixture->loop);
}

static void
cursor_test_alphabet_index (CursorFixture *fixture,
                            CursorClosure *closure,
                            CursorTest *test)
{
	ETestServerFixture      *server_fixture = (ETestServerFixture *) fixture;
	CursorTestAlphabetIndex *alphabet_index = (CursorTestAlphabetIndex *) test;

	if (closure->async) {

		AlphabetIndexReadyData data = { fixture, alphabet_index };

		e_book_client_cursor_set_alphabetic_index (
			fixture->cursor, alphabet_index->index,
			NULL, cursor_test_alphabet_index_ready_cb, &data);

		/* Wait for result with an error timeout */
		cursor_fixture_timeout_start (fixture, "Timeout reached while setting alphabet index");
		g_main_loop_run (server_fixture->loop);
		cursor_fixture_timeout_cancel (fixture);
	} else {
		GError *error = NULL;

		if (!e_book_client_cursor_set_alphabetic_index_sync (fixture->cursor,
								     alphabet_index->index,
								     NULL, &error))
			g_error ("Error setting alphabet index: %s", error->message);
	}
}

static void
cursor_test_alphabet_index_free (CursorTest *test)
{
	CursorTestAlphabetIndex   *alphabet_index = (CursorTestAlphabetIndex *) test;

	g_slice_free (CursorTestAlphabetIndex, alphabet_index);
}

/******************************************************
 *                Change Locale Tests                 *
 ******************************************************/
typedef struct {
	CursorTestType    type;

	gchar            *locale;
} CursorTestChangeLocale;

static void
cursor_closure_change_locale (CursorClosure *closure,
                              const gchar *locale)
{
	CursorTestChangeLocale *test = g_slice_new0 (CursorTestChangeLocale);

	test->type = CURSOR_TEST_CHANGE_LOCALE;
	test->locale = g_strdup (locale);

	closure->tests = g_list_append (closure->tests, test);
}

static void
cursor_test_change_locale (CursorFixture *fixture,
                           CursorClosure *closure,
                           CursorTest *test)
{
	CursorTestChangeLocale *change_locale = (CursorTestChangeLocale *) test;

	/* There is no sync/async for this */
	cursor_fixture_set_locale (fixture, change_locale->locale);
}

static void
cursor_test_change_locale_free (CursorTest *test)
{
	CursorTestChangeLocale *change_locale = (CursorTestChangeLocale *) test;

	g_free (change_locale->locale);
	g_slice_free (CursorTestChangeLocale, change_locale);
}

/******************************************************
 *                   Alphabet Tests                   *
 ******************************************************/
#define ALPHABET_TEST_LETTERS 5

typedef struct {
	CursorTestType    type;

	gchar            *letters[ALPHABET_TEST_LETTERS];
	gboolean          immediate;

	gulong            alphabet_id;
} CursorTestAlphabet;

typedef struct {
	CursorFixture      *fixture;
	CursorTestAlphabet *test;
} AlphabetData;

static void
cursor_closure_alphabet (CursorClosure *closure,
                         gboolean immediate,
                         const gchar *letter0,
                         const gchar *letter1,
                         const gchar *letter2,
                         const gchar *letter3,
                         const gchar *letter4)
{
	CursorTestAlphabet *test = g_slice_new0 (CursorTestAlphabet);

	test->type = CURSOR_TEST_ALPHABET;
	test->immediate = immediate;
	test->letters[0] = g_strdup (letter0);
	test->letters[1] = g_strdup (letter1);
	test->letters[2] = g_strdup (letter2);
	test->letters[3] = g_strdup (letter3);
	test->letters[4] = g_strdup (letter4);

	closure->tests = g_list_append (closure->tests, test);
}

static gboolean
expected_alpabet_check (EBookClientCursor *cursor,
                        CursorTestAlphabet *test,
                        gboolean assert_letters)
{
	const gchar * const * alphabet;
	gint i, n_labels = 0;
	gboolean expected = TRUE;

	alphabet = e_book_client_cursor_get_alphabet (cursor, &n_labels, NULL, NULL, NULL);

	for (i = 0; i < ALPHABET_TEST_LETTERS && expected; i++) {

		/* We compare letters[i] with alphabet[i + 1]
		 * so 'i' must fit into 'n_labels - 2'
		 */
		if (test->letters[i] != NULL && i > (n_labels - 2)) {

			if (assert_letters)
				g_error ("Not enough letters in the expected alphabet");
			else
				expected = FALSE;
		} else if (test->letters[i] != NULL) {

			if (assert_letters)
				g_assert_cmpstr (test->letters[i], ==, alphabet[i + 1]);
			else
				expected = (g_strcmp0 (test->letters[i], alphabet[i + 1]) == 0);
		}
	}

	return expected;
}

static void
alphabet_changed (EBookClientCursor *cursor,
                  GParamSpec *pspec,
                  AlphabetData *data)
{
	ETestServerFixture *server_fixture = (ETestServerFixture *) data->fixture;

	if (expected_alpabet_check (cursor, data->test, FALSE))
		g_main_loop_quit (server_fixture->loop);
}

static void
cursor_test_alphabet (CursorFixture *fixture,
                      CursorClosure *closure,
                      CursorTest *test)
{
	ETestServerFixture *server_fixture = (ETestServerFixture *) fixture;
	CursorTestAlphabet *alphabet = (CursorTestAlphabet *) test;
	AlphabetData        data = { fixture, alphabet };

	/* Alphabet is already correct */
	if (expected_alpabet_check (fixture->cursor, alphabet, alphabet->immediate))
		return;

	/* Alphabet is notified asynchronously, connect to signal and
	 * timeout error if the correct alphabet is never reached.
	 */
	alphabet->alphabet_id = g_signal_connect (
		fixture->cursor, "notify::alphabet",
		G_CALLBACK (alphabet_changed),
		&data);

	cursor_fixture_timeout_start (fixture, "Timeout waiting for expected alphabet");
	g_main_loop_run (server_fixture->loop);
	cursor_fixture_timeout_cancel (fixture);

	g_signal_handler_disconnect (fixture->cursor, alphabet->alphabet_id);
}

static void
cursor_test_alphabet_free (CursorTest *test)
{
	CursorTestAlphabet *alphabet = (CursorTestAlphabet *) test;
	gint i;

	for (i = 0; i < ALPHABET_TEST_LETTERS; i++)
		g_free (alphabet->letters[i]);

	g_slice_free (CursorTestAlphabet, alphabet);
}

/******************************************************
 *                     Thread Tests                   *
 ******************************************************/

struct _CursorThreadTest {
	CursorThreadFunc test_func;
	GDestroyNotify   destroy_data;
	gpointer         data;
};

struct _CursorThread {
	CursorTestType     type;

	CursorFixture     *fixture;
	CursorClosure     *closure;
	gchar             *thread_name;

	GThread           *thread;
	GMutex             mutex;
	GCond              cond;
	GMainContext      *context;
	GMainLoop         *loop;
	GSource           *timeout_source;

	EBookClientCursor *cursor;

	GList             *tests;

	guint              create_cursor : 1;
	guint              running : 1;
};

static CursorThread *
cursor_closure_thread (CursorClosure *closure,
                       gboolean create_cursor,
                       const gchar *format,
                       ...)
{
	CursorThread *test = g_slice_new0 (CursorThread);
	va_list args;

	test->type = CURSOR_TEST_THREAD;

	va_start (args, format);
	test->thread_name = g_strdup_vprintf (format, args);
	va_end (args);

	test->create_cursor = create_cursor;
	g_mutex_init (&test->mutex);
	g_cond_init (&test->cond);

	closure->tests = g_list_append (closure->tests, test);

	return test;
}

static gpointer
cursor_thread_func (gpointer data)
{
	CursorThread *thread = (CursorThread *) data;
	GList        *l;

	/* Allocate thread local resources */
	thread->context = g_main_context_new ();
	thread->loop = g_main_loop_new (thread->context, FALSE);
	g_main_context_push_thread_default (thread->context);

	if (thread->create_cursor) {
		/* Use a separate cursor from a different thread */
		ETestServerFixture  *server_fixture = (ETestServerFixture *) thread->fixture;
		EBookClient         *book_client;
		GError              *error;

		book_client = E_TEST_SERVER_UTILS_SERVICE (server_fixture, EBookClient);

		if (!e_book_client_get_cursor_sync (book_client,
						    NULL,
						    sort_fields,
						    sort_types,
						    N_SORT_FIELDS,
						    &thread->cursor,
						    NULL, &error))
			g_error ("Error creating cursor in thread: %s", error->message);

	} else {
		/* Use the same cursor from a different thread */
		thread->cursor = g_object_ref (thread->fixture->cursor);
	}

	/* Signal that we're running */
	g_mutex_lock (&thread->mutex);
	thread->running = TRUE;
	g_cond_signal (&thread->cond);
	g_mutex_unlock (&thread->mutex);

	/* Run tests */
	for (l = thread->tests; l; l = l->next) {
		CursorThreadTest *test = l->data;

		test->test_func (thread->fixture,
				 thread->closure,
				 thread,
				 test->data);
	}

	/* Cleanup and return */
	g_mutex_lock (&thread->mutex);

	g_object_unref (thread->cursor);
	g_main_context_pop_thread_default (thread->context);
	g_main_loop_unref (thread->loop);
	g_main_context_unref (thread->context);

	thread->context = NULL;
	thread->loop = NULL;
	thread->cursor = NULL;

	g_mutex_unlock (&thread->mutex);

	return NULL;
}

static void
cursor_test_thread (CursorFixture *fixture,
                    CursorClosure *closure,
                    CursorTest *test)
{
	CursorThread *thread = (CursorThread *) test;

	thread->fixture = fixture;
	thread->closure = closure;
	thread->thread = g_thread_new (
		thread->thread_name,
					cursor_thread_func,
					thread);

	/* Wait for the thread to start */
	g_mutex_lock (&thread->mutex);
	while (!thread->running)
		g_cond_wait (&thread->cond, &thread->mutex);
	g_mutex_unlock (&thread->mutex);
}

static void
cursor_thread_test_free (CursorThreadTest *thread_test)
{
	if (thread_test->destroy_data)
		thread_test->destroy_data (thread_test->data);

	g_slice_free (CursorThreadTest, thread_test);
}

static void
cursor_test_thread_free (CursorTest *test)
{
	CursorThread *thread = (CursorThread *) test;

	if (thread->thread)
		g_error ("Freeing thread test before the thread has been joined");

	g_free (thread->thread_name);

	g_mutex_clear (&thread->mutex);
	g_cond_clear (&thread->cond);

	g_list_free_full (thread->tests, (GDestroyNotify) cursor_thread_test_free);

	g_slice_free (CursorThread, thread);
}

static void
cursor_thread_add_test (CursorThread *thread,
                        CursorThreadFunc test_func,
                        gpointer data,
                        GDestroyNotify destroy_data)
{
	CursorThreadTest *thread_test = g_slice_new0 (CursorThreadTest);

	thread_test->test_func = test_func;
	thread_test->destroy_data = destroy_data;
	thread_test->data = data;

	thread->tests = g_list_append (thread->tests, thread_test);
}

static void
cursor_thread_timeout_start (CursorThread *thread,
                             const gchar *error_message)
{
	timeout_start (thread->context, &thread->timeout_source, error_message);
}

static void
cursor_thread_timeout_cancel (CursorThread *thread)
{
	timeout_cancel (&thread->timeout_source);
}

/******************************************************
 *                  Thread Join Tests                 *
 ******************************************************/
typedef struct {
	CursorTestType  type;

	CursorThread   *thread;
} CursorThreadJoin;

static void
cursor_closure_thread_join (CursorClosure *closure,
                            CursorThread *thread)
{
	CursorThreadJoin *test = g_slice_new0 (CursorThreadJoin);

	test->type = CURSOR_TEST_THREAD_JOIN;
	test->thread = thread;

	closure->tests = g_list_append (closure->tests, test);
}

static gboolean
quit_thread_cb (gpointer data)
{
	CursorThread   *thread = (CursorThread   *) data;

	g_main_loop_quit (thread->loop);

	return FALSE;
}

static void
cursor_test_thread_join (CursorFixture *fixture,
                         CursorClosure *closure,
                         CursorTest *test)
{
	CursorThreadJoin *join = (CursorThreadJoin *) test;
	GSource *source;

	g_mutex_lock (&(join->thread->mutex));
	if (join->thread->context) {
		source = g_idle_source_new ();
		g_source_set_callback (source, quit_thread_cb, join->thread, NULL);
		g_source_attach (source, join->thread->context);
		g_source_unref (source);
	}
	g_mutex_unlock (&(join->thread->mutex));

	g_thread_join (join->thread->thread);
	join->thread->thread = NULL;
}

static void
cursor_test_thread_join_free (CursorTest *test)
{
	CursorThreadJoin *join = (CursorThreadJoin *) test;

	g_slice_free (CursorThreadJoin, join);
}

/******************************************************
 *                   Tests                 *
 ******************************************************/
typedef struct {
	CursorTestType    type;

	guint             ms;
} CursorTestSpinLoop;

static void
cursor_closure_spin_loop (CursorClosure *closure,
                          guint ms)
{
	CursorTestSpinLoop *test = g_slice_new0 (CursorTestSpinLoop);

	test->type = CURSOR_TEST_SPIN_LOOP;
	test->ms = ms;

	closure->tests = g_list_append (closure->tests, test);
}

static gboolean
quit_loop_cb (gpointer data)
{
	GMainLoop *loop = (GMainLoop *) data;

	g_main_loop_quit (loop);
	return FALSE;
}

static void
cursor_test_spin_loop (CursorFixture *fixture,
                       CursorClosure *closure,
                       CursorTest *test)
{
	ETestServerFixture *server_fixture = (ETestServerFixture *) fixture;
	CursorTestSpinLoop *spin = (CursorTestSpinLoop *) test;

	if (spin->ms > 0)
		g_timeout_add (spin->ms, quit_loop_cb, server_fixture->loop);
	else
		g_idle_add (quit_loop_cb, server_fixture->loop);

	g_main_loop_run (server_fixture->loop);
}

static void
cursor_test_spin_loop_free (CursorTest *test)
{
	CursorTestSpinLoop *spin = (CursorTestSpinLoop *) test;

	g_slice_free (CursorTestSpinLoop, spin);
}

/******************************************************
 *                Threaded Test Functions             *
 ******************************************************/
typedef struct {
	CursorFixture  *fixture;
	CursorClosure  *closure;
	CursorThread   *thread;

	guint           async : 1;
	guint           out_of_sync : 1;
	guint           end_of_list : 1;
} StepLoopData;

static void
step_loop_ready_cb (GObject *source_object,
                    GAsyncResult *result,
                    gpointer user_data)
{
	StepLoopData *data = (StepLoopData *) user_data;
	GSList       *results = NULL;
	GError       *error = NULL;

	if (e_book_client_cursor_step_finish (E_BOOK_CLIENT_CURSOR (source_object),
						 result, &results, &error) < 0) {

		if (g_error_matches (error,
				     E_CLIENT_ERROR,
				     E_CLIENT_ERROR_OUT_OF_SYNC)) {
			data->out_of_sync = TRUE;
			g_clear_error (&error);
		} else if (g_error_matches (error,
					    E_CLIENT_ERROR,
					    E_CLIENT_ERROR_QUERY_REFUSED)) {
			data->end_of_list = TRUE;
			g_clear_error (&error);
		} else {
			g_error (
				"Error calling e_book_client_cursor_step_finish(): %s",
				error->message);
		}
	}

	g_slist_free_full (results, (GDestroyNotify) g_object_unref);
	g_main_loop_quit (data->thread->loop);
}

static void
step_loop_refreshed (EBookClientCursor *cursor,
                     StepLoopData *data)
{
	g_main_loop_quit (data->thread->loop);
}

static void
step_loop_iteration (StepLoopData *data)
{
	EBookCursorOrigin origin = E_BOOK_CURSOR_ORIGIN_CURRENT;
	GError *error = NULL;
	GSList *results = NULL;

	if (data->end_of_list) {
		origin = E_BOOK_CURSOR_ORIGIN_BEGIN;
		data->end_of_list = FALSE;
	}

	if (data->async) {

		e_book_client_cursor_step (
			data->thread->cursor,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			origin,
			3,
			NULL,
			step_loop_ready_cb,
			data);

		cursor_thread_timeout_start (data->thread, "Timeout reached waiting for step() reply in a thread");
		g_main_loop_run (data->thread->loop);
		cursor_thread_timeout_cancel (data->thread);
	} else {

		if (e_book_client_cursor_step_sync (data->thread->cursor,
						    E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
						    origin,
						    3,
						    &results,
						    NULL, &error) < 0) {

			if (g_error_matches (error,
					     E_CLIENT_ERROR,
					     E_CLIENT_ERROR_OUT_OF_SYNC)) {
				data->out_of_sync = TRUE;
				g_clear_error (&error);
			} else if (g_error_matches (error,
						    E_CLIENT_ERROR,
						    E_CLIENT_ERROR_QUERY_REFUSED)) {
				data->end_of_list = TRUE;
				g_clear_error (&error);
			} else {
				g_error (
					"Error calling e_book_client_cursor_step_sync(): %s",
					error->message);
			}
		}

		g_slist_free_full (results, (GDestroyNotify) g_object_unref);
	}

	/* Pause here and wait for a refresh if we're out of sync */
	if (data->out_of_sync) {

		gulong refresh_id = g_signal_connect (
			data->thread->cursor, "refresh",
			G_CALLBACK (step_loop_refreshed),
			data);

		cursor_thread_timeout_start (
			data->thread,
			"Timeout reached waiting for a refresh "
			"event after an out-of-sync return by step()");
		g_main_loop_run (data->thread->loop);
		cursor_thread_timeout_cancel (data->thread);

		g_signal_handler_disconnect (data->thread->cursor, refresh_id);

		data->out_of_sync = FALSE;
	}
}

static void
step_loop (CursorFixture *fixture,
              CursorClosure *closure,
              CursorThread *thread,
              gpointer user_data)
{
	gboolean async = GPOINTER_TO_INT (user_data);
	StepLoopData data = { fixture, closure, thread, async, FALSE, FALSE };
	gint i;

	for (i = 0; i < THREAD_ITERATIONS; i++) {
		step_loop_iteration (&data);
	}
}

/******************************************************
 *                Main, tests defined here            *
 ******************************************************/
typedef struct {
	const gchar *base_path;
	gboolean     async;
	gboolean     dedicated_cursor;
} ThreadParams;

static CursorParams base_params[] = {
	{ "/FullSummary/Standard/Sync",    FALSE, FALSE, FALSE },
	{ "/FullSummary/Standard/Async",   TRUE,  FALSE, FALSE },
	{ "/FullSummary/Direct/Sync",      FALSE, TRUE,  FALSE },
	{ "/FullSummary/Direct/Async",     TRUE,  TRUE,  FALSE },
	{ "/EmptySummary/Standard/Sync",   FALSE, FALSE, TRUE },
	{ "/EmptySummary/Standard/Async",  TRUE,  FALSE, TRUE },
	{ "/EmptySummary/Direct/Sync",     FALSE, TRUE,  TRUE },
	{ "/EmptySummary/Direct/Async",    TRUE,  TRUE,  TRUE },
};

static ThreadParams thread_params[] = {
	{ "/SharedCursor/ThreadSync",         FALSE, FALSE },
	{ "/SharedCursor/ThreadAsync",        TRUE, FALSE },
	{ "/DedicatedCursor/ThreadSync",      FALSE, TRUE },
	{ "/DedicatedCursor/ThreadAsync",     TRUE, TRUE },
};

gint
main (gint argc,
      gchar **argv)
{
	GOptionContext *context;
	CursorClosure *closure;
	CursorThread  *thread;
	CursorThread  *threads[CONCURRENT_THREADS];
	gint           i, j, k;

	/* Parse our regex first */
	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, entries, GETTEXT_PACKAGE);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	if (test_filter)
		test_regex = g_regex_new (test_filter, 0, 0, NULL);

	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	for (i = 0; i < G_N_ELEMENTS (base_params); i++) {

		/****************************************************
		 *             BASIC SORT ORDERING TESTS            *
		 ****************************************************
		 *
		 * Note that all sort ordering can be confirmed with the
		 * chart found in tests/libedata-book/data-test-utils.h
		 */

		/* POSIX Order */
		closure = cursor_closure_new (&base_params[i], "POSIX");
		cursor_closure_alphabet (closure, TRUE, "A", "B", "C", "D", "E");
		cursor_closure_position (closure, 20, 0, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_BEGIN,
			10, /* Count */
			10, /* Expected results */
			11, 2,  6,  3,  8, 1,  5,  4,  7,  15);
		cursor_closure_position (closure, 20, 10, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_CURRENT,
			10, /* Count */
			10, /* Expected results */
			17, 16, 18, 10, 14, 12, 13, 9,  19, 20);
		cursor_closure_position (closure, 20, 20, TRUE);
		cursor_closure_add (closure, "/EBookClientCursor/Order/POSIX%s", base_params[i].base_path);

		/* en_US Order */
		closure = cursor_closure_new (&base_params[i], "en_US.utf8");
		cursor_closure_alphabet (closure, TRUE, "A", "B", "C", "D", "E");
		cursor_closure_position (closure, 20, 0, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_BEGIN,
			10, /* Count */
			10, /* Expected results */
			11, 1,  2,  5,  6, 4,  3,  7,  8,  15);
		cursor_closure_position (closure, 20, 10, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_CURRENT,
			10, /* Count */
			10, /* Expected results */
			17, 16, 18, 10, 14, 12, 13, 9,  19, 20);
		cursor_closure_position (closure, 20, 20, TRUE);
		cursor_closure_add (closure, "/EBookClientCursor/Order/en_US%s", base_params[i].base_path);

		/* fr_CA Order */
		closure = cursor_closure_new (&base_params[i], "fr_CA.utf8");
		cursor_closure_alphabet (closure, TRUE, "A", "B", "C", "D", "E");
		cursor_closure_position (closure, 20, 0, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_BEGIN,
			10, /* Count */
			10, /* Expected results */
			11, 1,  2,  5,  6, 4,  3,  7,  8,  15);
		cursor_closure_position (closure, 20, 10, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_CURRENT,
			10, /* Count */
			10, /* Expected results */
			17, 16, 18, 10, 14, 13, 12, 9,  19, 20);
		cursor_closure_position (closure, 20, 20, TRUE);
		cursor_closure_add (closure, "/EBookClientCursor/Order/fr_CA%s", base_params[i].base_path);

		/* de_DE Order */
		closure = cursor_closure_new (&base_params[i], "de_DE.utf8");
		cursor_closure_alphabet (closure, TRUE, "A", "B", "C", "D", "E");
		cursor_closure_position (closure, 20, 0, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_BEGIN,
			10, /* Count */
			10, /* Expected results */
			11, 1,  2,  5,  6, 7,  8,  4,  3,  15);
		cursor_closure_position (closure, 20, 10, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_CURRENT,
			10, /* Count */
			10, /* Expected results */
			17, 16, 18, 10, 14, 12, 13, 9,  20, 19);
		cursor_closure_position (closure, 20, 20, TRUE);
		cursor_closure_add (closure, "/EBookClientCursor/Order/de_DE%s", base_params[i].base_path);

		/****************************************************
		 *                  Step / Origins Test             *
		 ****************************************************/

		/* Overshooting the contact list causes position to become total + 1 */
		closure = cursor_closure_new (&base_params[i], "POSIX");
		cursor_closure_position (closure, 20, 0, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_BEGIN,
			10, /* Count */
			10, /* Expected results */
			11, 2,  6,  3,  8, 1,  5,  4,  7,  15);
		cursor_closure_position (closure, 20, 10, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_CURRENT,
			11, /* Count */
			10, /* Expected results */
			17, 16, 18, 10, 14, 12, 13, 9,  19, 20);
		cursor_closure_position (closure, 20, 21, TRUE);
		cursor_closure_add (closure, "/EBookClientCursor/Step/Overshoot%s", base_params[i].base_path);

		/* Undershooting the contact list (in reverse) causes position to become 0 */
		closure = cursor_closure_new (&base_params[i], "POSIX");
		cursor_closure_position (closure, 20, 0, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_END,
			-10, /* Count */
			10, /* Expected results */
			20, 19, 9, 13, 12, 14, 10, 18, 16, 17);
		cursor_closure_position (closure, 20, 11, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_CURRENT,
			-11, /* Count */
			10, /* Expected results */
			15, 7, 4, 5, 1, 8, 3, 6, 2, 11);
		cursor_closure_position (closure, 20, 0, TRUE);
		cursor_closure_add (closure, "/EBookClientCursor/Step/Undershoot%s", base_params[i].base_path);

		/* Stepping past the end position causes an E_CLIENT_ERROR_QUERY_REFUSED */
		closure = cursor_closure_new (&base_params[i], "POSIX");
		cursor_closure_position (closure, 20, 0, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_BEGIN,
			10, /* Count */
			10, /* Expected results */
			11, 2,  6,  3,  8, 1,  5,  4,  7,  15);
		cursor_closure_position (closure, 20, 10, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_CURRENT,
			11, /* Count */
			10, /* Expected results */
			17, 16, 18, 10, 14, 12, 13, 9,  19, 20);
		cursor_closure_position (closure, 20, 21, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_CURRENT,
			1,   /* Count */
			-1); /* Expect E_CLIENT_ERROR_QUERY_REFUSED */
		cursor_closure_add (closure, "/EBookClientCursor/Step/EndOfListError/End%s", base_params[i].base_path);

		/* Stepping backwards past the beginning position causes an E_CLIENT_ERROR_QUERY_REFUSED */
		closure = cursor_closure_new (&base_params[i], "POSIX");
		cursor_closure_position (closure, 20, 0, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_END,
			-10, /* Count */
			10, /* Expected results */
			20, 19, 9, 13, 12, 14, 10, 18, 16, 17);
		cursor_closure_position (closure, 20, 11, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_CURRENT,
			-11, /* Count */
			10, /* Expected results */
			15, 7, 4, 5, 1, 8, 3, 6, 2, 11);
		cursor_closure_position (closure, 20, 0, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_CURRENT,
			-1,  /* Count */
			-1); /* Expect E_CLIENT_ERROR_QUERY_REFUSED */
		cursor_closure_add (closure, "/EBookClientCursor/Step/EndOfListError/Begin%s", base_params[i].base_path);

		/* Resetting query to get the beginning of the results */
		closure = cursor_closure_new (&base_params[i], "POSIX");
		cursor_closure_position (closure, 20, 0, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_BEGIN,
			10, /* Count */
			10, /* Expected results */
			11, 2,  6,  3,  8, 1,  5,  4,  7,  15);
		cursor_closure_position (closure, 20, 10, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_BEGIN,
			10, /* Count */
			10, /* Expected results */
			11, 2,  6,  3,  8, 1,  5,  4,  7,  15);
		cursor_closure_position (closure, 20, 10, TRUE);
		cursor_closure_add (closure, "/EBookClientCursor/Step/Reset/Forward%s", base_params[i].base_path);

		/* Resetting query to get the ending of the results (backwards) */
		closure = cursor_closure_new (&base_params[i], "POSIX");
		cursor_closure_position (closure, 20, 0, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_END,
			-10, /* Count */
			10, /* Expected results */
			20, 19, 9, 13, 12, 14, 10, 18, 16, 17);
		cursor_closure_position (closure, 20, 11, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_END,
			-10, /* Count */
			10, /* Expected results */
			20, 19, 9, 13, 12, 14, 10, 18, 16, 17);
		cursor_closure_position (closure, 20, 11, TRUE);
		cursor_closure_add (closure, "/EBookClientCursor/Step/Reset/Backwards%s", base_params[i].base_path);

		/* Move twice and then repeat query */
		closure = cursor_closure_new (&base_params[i], "POSIX");
		cursor_closure_position (closure, 20, 0, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_BEGIN,
			10, /* Count */
			10, /* Expected results */
			11, 2,  6,  3,  8, 1,  5,  4,  7,  15);
		cursor_closure_position (closure, 20, 10, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_CURRENT,
			5, /* Count */
			5, /* Expected results */
			17, 16, 18, 10, 14);
		cursor_closure_position (closure, 20, 10, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_CURRENT,
			5, /* Count */
			5, /* Expected results */
			17, 16, 18, 10, 14);
		cursor_closure_position (closure, 20, 15, TRUE);
		cursor_closure_add (closure, "/EBookClientCursor/Step/RepeatPrevious/Forward%s", base_params[i].base_path);

		/* Move twice and then repeat query */
		closure = cursor_closure_new (&base_params[i], "POSIX");
		cursor_closure_position (closure, 20, 0, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_END,
			-10, /* Count */
			10, /* Expected results */
			20, 19, 9, 13, 12, 14, 10, 18, 16, 17);
		cursor_closure_position (closure, 20, 11, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_CURRENT,
			-5, /* Count */
			5, /* Expected results */
			15, 7, 4, 5, 1);
		cursor_closure_position (closure, 20, 11, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_CURRENT,
			-5, /* Count */
			5, /* Expected results */
			15, 7, 4, 5, 1);
		cursor_closure_position (closure, 20, 6, TRUE);
		cursor_closure_add (closure, "/EBookClientCursor/Step/RepeatPrevious/Backwards%s", base_params[i].base_path);

		/****************************************************
		 *           BASIC SEARCH EXPRESSION TESTS          *
		 ****************************************************/

		/* Query Changes Position */
		closure = cursor_closure_new (&base_params[i], "POSIX");
		cursor_closure_position (closure, 20, 0, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_BEGIN,
			10, /* Count */
			10, /* Expected results */
			11, 2,  6,  3,  8, 1,  5,  4,  7,  15);
		cursor_closure_position (closure, 20, 10, TRUE);
		cursor_closure_set_sexp (
			closure,
			e_book_query_field_test (
						E_CONTACT_EMAIL,
						E_BOOK_QUERY_ENDS_WITH,
						".com"));

		/* In POSIX Locale, the 10th contact out of 20 becomes the 6th contact out of
		 * 13 contacts bearing a '.com' email address
		 */
		cursor_closure_position (closure, 13, 6, TRUE);
		cursor_closure_add (closure, "/EBookClientCursor/SearchExpression/EffectsPosition%s", base_params[i].base_path);

		/****************************************************
		 *             Address Book Modifications           *
		 ****************************************************/

		/* Test that adding a contact changes the total / position appropriately */
		closure = cursor_closure_new (&base_params[i], "POSIX");
		cursor_closure_position (closure, 20, 0, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_BEGIN,
			10, /* Count */
			10, /* Expected results */
			11, 2,  6,  3,  8, 1,  5,  4,  7,  15);
		cursor_closure_position (closure, 20, 10, TRUE);
		cursor_closure_add_contact (closure, "custom-3");
		cursor_closure_position (closure, 21, 11, FALSE);
		cursor_closure_add (closure, "/EBookClientCursor/AddContact/TestForward%s", base_params[i].base_path);

		/* Test that adding a contact changes the total / position appropriately after having moved from the end */
		closure = cursor_closure_new (&base_params[i], "POSIX");
		cursor_closure_position (closure, 20, 0, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_END,
			-10, /* Count */
			10, /* Expected results */
			20, 19, 9, 13, 12, 14, 10, 18, 16, 17);
		cursor_closure_position (closure, 20, 11, TRUE);
		cursor_closure_add_contact (closure, "custom-3");
		cursor_closure_position (closure, 21, 12, FALSE);
		cursor_closure_add (closure, "/EBookClientCursor/AddContact/TestBackwards%s", base_params[i].base_path);

		/* Test that removing a contact changes the total / position appropriately */
		closure = cursor_closure_new (&base_params[i], "POSIX");
		cursor_closure_position (closure, 20, 0, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_BEGIN,
			10, /* Count */
			10, /* Expected results */
			11, 2,  6,  3,  8, 1,  5,  4,  7,  15);
		cursor_closure_position (closure, 20, 10, TRUE);
		cursor_closure_remove_contact (closure, "sorted-14");
		cursor_closure_position (closure, 19, 10, FALSE);
		cursor_closure_add (closure, "/EBookClientCursor/RemoveContact/TestForward%s", base_params[i].base_path);

		/* Test that removing a contact changes the total / position appropriately after having moved from the end */
		closure = cursor_closure_new (&base_params[i], "POSIX");
		cursor_closure_position (closure, 20, 0, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_END,
			-10, /* Count */
			10, /* Expected results */
			20, 19, 9, 13, 12, 14, 10, 18, 16, 17);
		cursor_closure_position (closure, 20, 11, TRUE);
		cursor_closure_remove_contact (closure, "sorted-14");
		cursor_closure_position (closure, 19, 11, FALSE);
		cursor_closure_add (closure, "/EBookClientCursor/RemoveContact/TestBackwards%s", base_params[i].base_path);

		/****************************************************
		 *                Alphabet Index Tests              *
		 ****************************************************/

		/* POSIX locale & Latin indexes */
		closure = cursor_closure_new (&base_params[i], "POSIX");
		cursor_closure_position (closure, 20, 0, TRUE);
		cursor_closure_alphabet_index (closure, 2);
		cursor_closure_position (closure, 20, 1, TRUE);
		cursor_closure_add (closure, "/EBookClientCursor/AlphabetIndex/Position/B%s", base_params[i].base_path);

		closure = cursor_closure_new (&base_params[i], "POSIX");
		cursor_closure_position (closure, 20, 0, TRUE);
		cursor_closure_alphabet_index (closure, 3);
		cursor_closure_position (closure, 20, 13, TRUE);
		cursor_closure_add (closure, "/EBookClientCursor/AlphabetIndex/Position/C%s", base_params[i].base_path);

		closure = cursor_closure_new (&base_params[i], "POSIX");
		cursor_closure_position (closure, 20, 0, TRUE);
		cursor_closure_alphabet_index (closure, 13);
		cursor_closure_position (closure, 20, 18, TRUE);
		cursor_closure_add (closure, "/EBookClientCursor/AlphabetIndex/Position/M%s", base_params[i].base_path);

		/****************************************************
		 *               Dynamic Locale Changes             *
		 ****************************************************
		 *
		 * Locale changes take effect asynchronously, so we have to
		 * check that the total/position automatically resets itself
		 * asynchronously, not synchronously.
		 *
		 * Other direct e_book_client_cursor_() apis however result
		 * in synchronous updates of the position / total values.
		 *
		 * These tests add an idle pause after setting the locale
		 * using 'cursor_closure_spin_loop (closure, 0)'.
		 *
		 * The locale setting test only blocks until the EBookClient
		 * receives a new locale, but the EBookClientCursor::alphabet
		 * notification may have not been notified, so we add this
		 * idle to ensure that it is before adding any assertions.
		 */

		/* Start in POSIX */
		closure = cursor_closure_new (&base_params[i], "POSIX");
		cursor_closure_position (closure, 20, 0, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_BEGIN,
			10, /* Count */
			10, /* Expected results */
			11, 2,  6,  3,  8, 1,  5,  4,  7,  15);
		cursor_closure_position (closure, 20, 10, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_CURRENT,
			10, /* Count */
			10, /* Expected results */
			17, 16, 18, 10, 14, 12, 13, 9,  19, 20);
		cursor_closure_position (closure, 20, 20, TRUE);

		/* Now in en_US */
		cursor_closure_change_locale (closure, "en_US.utf8");
		cursor_closure_spin_loop (closure, 0);
		cursor_closure_position (closure, 20, 0, FALSE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_BEGIN,
			10, /* Count */
			10, /* Expected results */
			11, 1,  2,  5,  6, 4,  3,  7,  8,  15);
		cursor_closure_position (closure, 20, 10, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_CURRENT,
			10, /* Count */
			10, /* Expected results */
			17, 16, 18, 10, 14, 12, 13, 9,  19, 20);
		cursor_closure_position (closure, 20, 20, TRUE);

		/* Now in fr_CA */
		cursor_closure_change_locale (closure, "fr_CA.utf8");
		cursor_closure_spin_loop (closure, 0);
		cursor_closure_position (closure, 20, 0, FALSE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_BEGIN,
			10, /* Count */
			10, /* Expected results */
			11, 1,  2,  5,  6, 4,  3,  7,  8,  15);
		cursor_closure_position (closure, 20, 10, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_CURRENT,
			10, /* Count */
			10, /* Expected results */
			17, 16, 18, 10, 14, 13, 12, 9,  19, 20);
		cursor_closure_position (closure, 20, 20, TRUE);

		/* Now in de_DE */
		cursor_closure_change_locale (closure, "de_DE.utf8");
		cursor_closure_spin_loop (closure, 0);
		cursor_closure_position (closure, 20, 0, FALSE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_BEGIN,
			10, /* Count */
			10, /* Expected results */
			11, 1,  2,  5,  6, 7,  8,  4,  3,  15);
		cursor_closure_position (closure, 20, 10, TRUE);
		cursor_closure_step (
			closure,
			E_BOOK_CURSOR_STEP_MOVE | E_BOOK_CURSOR_STEP_FETCH,
			E_BOOK_CURSOR_ORIGIN_CURRENT,
			10, /* Count */
			10, /* Expected results */
			17, 16, 18, 10, 14, 12, 13, 9,  20, 19);
		cursor_closure_position (closure, 20, 20, TRUE);
		cursor_closure_add (closure, "/EBookClientCursor/ChangeLocale/Order%s", base_params[i].base_path);

		/*
		 * Check that alphabets are updated after changing locale
		 *
		 * For a variety of locales, check first that the alphabet is latin for
		 * "POSIX" locale (synchronously), and then check that the alphabet changes
		 * to the new expected alphabet for the new locale after dynamically changing
		 * the locale (allow a timeout, alphabet changes are delivered only asynchronously).
		 */
		closure = cursor_closure_new (&base_params[i], "POSIX");
		cursor_closure_alphabet (closure, TRUE, "A", "B", "C", "D", "E");
		cursor_closure_change_locale (closure, "en_US.utf8");
		cursor_closure_alphabet (closure, FALSE, "A", "B", "C", "D", "E");
		cursor_closure_add (closure, "/EBookClientCursor/ChangeLocale/Alphabet/en_US%s", base_params[i].base_path);

		closure = cursor_closure_new (&base_params[i], "POSIX");
		cursor_closure_alphabet (closure, TRUE, "A", "B", "C", "D", "E");
		cursor_closure_change_locale (closure, "el_GR.utf8");
		cursor_closure_alphabet (closure, FALSE, "Α", "Β", "Γ", "Δ", "Ε");
		cursor_closure_add (closure, "/EBookClientCursor/ChangeLocale/Alphabet/el_GR%s", base_params[i].base_path);

		closure = cursor_closure_new (&base_params[i], "POSIX");
		cursor_closure_alphabet (closure, TRUE, "A", "B", "C", "D", "E");
		cursor_closure_change_locale (closure, "ru_RU.utf8");
		cursor_closure_alphabet (closure, FALSE, "А", "Б", "В", "Г", "Д");
		cursor_closure_add (closure, "/EBookClientCursor/ChangeLocale/Alphabet/ru_RU%s", base_params[i].base_path);

		closure = cursor_closure_new (&base_params[i], "POSIX");
		cursor_closure_alphabet (closure, TRUE, "A", "B", "C", "D", "E");
		cursor_closure_change_locale (closure, "ja_JP.utf8");
		cursor_closure_alphabet (closure, FALSE, "あ", "か", "さ", "た", "な");
		cursor_closure_add (closure, "/EBookClientCursor/ChangeLocale/Alphabet/ja_JP%s", base_params[i].base_path);

		/* The alphabet doesnt change for chinese */
		closure = cursor_closure_new (&base_params[i], "POSIX");
		cursor_closure_alphabet (closure, TRUE, "A", "B", "C", "D", "E");
		cursor_closure_change_locale (closure, "zh_CN.utf8");
		cursor_closure_alphabet (closure, FALSE, "A", "B", "C", "D", "E");
		cursor_closure_add (closure, "/EBookClientCursor/ChangeLocale/Alphabet/zh_CN%s", base_params[i].base_path);

		closure = cursor_closure_new (&base_params[i], "POSIX");
		cursor_closure_alphabet (closure, TRUE, "A", "B", "C", "D", "E");
		cursor_closure_change_locale (closure, "ko_KR.utf8");
		cursor_closure_alphabet (closure, FALSE, "ᄀ", "ᄂ", "ᄃ", "ᄅ", "ᄆ");
		cursor_closure_add (closure, "/EBookClientCursor/ChangeLocale/Alphabet/ko_KR%s", base_params[i].base_path);

		closure = cursor_closure_new (&base_params[i], "POSIX");
		cursor_closure_alphabet (closure, TRUE, "A", "B", "C", "D", "E");
		cursor_closure_change_locale (closure, "ar_TN.utf8");
		cursor_closure_alphabet (closure, FALSE, "ا", "ب", "ت", "ث", "ج");
		cursor_closure_add (closure, "/EBookClientCursor/ChangeLocale/Alphabet/ar_TN%s", base_params[i].base_path);

		/****************************************************
		 *                   Threaded Tests                 *
		 ****************************************************/

		for (j = 0; j < G_N_ELEMENTS (thread_params); j++) {

			/*
			 * This test performs a series of cursor moves in a dedicated
			 * thread while the main thread is repeatedly modifying the addressbook
			 *
			 * The step_loop() will encounter races where it finds that the addressbook
			 * has been modified, when E_CLIENT_ERROR_OUT_OF_SYNC is reported then it will
			 * wait for a refresh and then continue.
			 *
			 * The test will fail if it crashes from unprotected concurrent memory accesses,
			 * and it will fail if ever an out-of-sync error is not followed by an asynchronous
			 * "refresh" signal.
			 */
			closure = cursor_closure_new (&base_params[i], "POSIX");
			thread = cursor_closure_thread (closure, thread_params[j].dedicated_cursor, "move-by-thread");
			cursor_thread_add_test (
				thread, step_loop,
						GINT_TO_POINTER (thread_params[j].async), NULL);

			for (k = 0; k < THREAD_ITERATIONS; k++) {
				cursor_closure_add_contact (closure, "custom-3");
				cursor_closure_spin_loop (closure, 0);
				cursor_closure_remove_contact (closure, "custom-3");
				cursor_closure_spin_loop (closure, 0);
			}
			cursor_closure_thread_join (closure, thread);
			cursor_closure_add (
				closure, "/EBookClientCursor/Thread/Step/AddingAndRemoving%s%s",
				base_params[i].base_path,
				thread_params[j].base_path);

			/* The same test as above, except that the addressbook is changing locale instead
			 * of adding / removing contacts
			 */
			closure = cursor_closure_new (&base_params[i], "POSIX");
			thread = cursor_closure_thread (closure, thread_params[j].dedicated_cursor, "move-by-thread");
			cursor_thread_add_test (
				thread, step_loop,
						GINT_TO_POINTER (thread_params[j].async), NULL);

			for (k = 0; k < THREAD_ITERATIONS; k++) {
				cursor_closure_change_locale (closure, "de_DE.utf8");
				cursor_closure_spin_loop (closure, 0);
				cursor_closure_change_locale (closure, "fr_CA.utf8");
				cursor_closure_spin_loop (closure, 0);
			}
			cursor_closure_thread_join (closure, thread);
			cursor_closure_add (
				closure, "/EBookClientCursor/Thread/Step/ChangingLocale%s%s",
				base_params[i].base_path,
				thread_params[j].base_path);

			/* Add / Remove contacts with multiple threaded step() operations going on concurrently */
			closure = cursor_closure_new (&base_params[i], "POSIX");

			for (k = 0; k < CONCURRENT_THREADS; k++) {
				threads[k] = cursor_closure_thread (closure, thread_params[j].dedicated_cursor, "move-by-thread");
				cursor_thread_add_test (
					threads[k], step_loop,
							GINT_TO_POINTER (thread_params[j].async), NULL);
			}

			for (k = 0; k < THREAD_ITERATIONS; k++) {
				cursor_closure_add_contact (closure, "custom-3");
				cursor_closure_spin_loop (closure, 0);
				cursor_closure_remove_contact (closure, "custom-3");
				cursor_closure_spin_loop (closure, 0);
			}

			for (k = 0; k < CONCURRENT_THREADS; k++)
				cursor_closure_thread_join (closure, threads[k]);

			cursor_closure_add (
				closure, "/EBookClientCursor/MultipleThreads/Step/AddingAndRemoving%s%s",
				base_params[i].base_path,
				thread_params[j].base_path);

		}
	}

	return e_test_server_utils_run ();
}
