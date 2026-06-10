/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

/* folder/index testing */

#include <string.h>

#include "camel-test.h"
#include "camel-test-provider.h"
#include "messages.h"
#include "folders.h"
#include "session.h"

static const gchar *local_drivers[] = { "local" };

static struct {
	const gchar *name;
	CamelFolder *folder;
} mailboxes[] = {
	{ "INBOX", NULL },
	{ "folder1", NULL },
	{ "folder2", NULL },
	{ "folder3", NULL },
	{ "folder4", NULL },
};

static struct {
	const gchar *name, *match, *action;
} rules[] = {
	{ "empty1", "(match-all (header-contains \"Frobnitz\"))", "(copy-to \"folder1\")" },
	{ "empty2", "(header-contains \"Frobnitz\")", "(copy-to \"folder2\")" },
	{ "count11", "(and (header-contains \"subject\" \"Test1\") (header-contains \"subject\" \"subject\"))", "(move-to \"folder3\")" },
	{ "empty3", "(and (header-contains \"subject\" \"Test1\") (header-contains \"subject\" \"subject\"))", "(move-to \"folder4\")" },
	{ "count1", "(body-contains \"data50\")", "(copy-to \"folder1\")" },
	{ "stop", "(body-contains \"data2\")", "(stop)" },
	{ "notreached1", "(body-contains \"data2\")", "(move-to \"folder2\")" },
	{ "count1", "(body-contains \"data3\")", "(move-to \"folder2\")" },
	{ "ustrcasecmp", "(header-matches \"Subject\" \"Test0 message100 subject\")", "(copy-to \"folder2\")" },
};

/* broken match rules */
static struct {
	const gchar *name, *match, *action;
} brokens[] = {
	{ "count1", "(body-contains data50)", "(copy-to \"folder1\")" }, /* non string argument */
	{ "count1", "(body-contains-stuff \"data3\")", "(move-to-folder \"folder2\")" }, /* invalid function */
	{ "count1", "(or (body-contains \"data3\") (foo))", "(move-to-folder \"folder2\")" }, /* invalid function */
	{ "count1", "(or (body-contains \"data3\") (foo)", "(move-to-folder \"folder2\")" }, /* missing ) */
	{ "count1", "(and body-contains \"data3\") (foo)", "(move-to-folder \"folder2\")" }, /* missing ( */
	{ "count1", "body-contains \"data3\")", "(move-to-folder \"folder2\")" }, /* missing ( */
	{ "count1", "body-contains \"data3\"", "(move-to-folder \"folder2\")" }, /* missing ( ) */
	{ "count1", "(body-contains \"data3\" ())", "(move-to-folder \"folder2\")" }, /* extra () */
	{ "count1", "()", "(move-to-folder \"folder2\")" }, /* invalid () */
	{ "count1", "", "(move-to-folder \"folder2\")" }, /* empty */
};

/* broken action rules */
static struct {
	const gchar *name, *match, *action;
} brokena[] = {
	{ "a", "(body-contains \"data2\")", "(body-contains \"help\")" }, /* rule in action */
	{ "a", "(body-contains \"data2\")", "(move-to-folder-name \"folder2\")" }, /* unknown function */
	{ "a", "(body-contains \"data2\")", "(or (move-to-folder \"folder2\")" }, /* missing ) */
	{ "a", "(body-contains \"data2\")", "(or move-to-folder \"folder2\"))" }, /* missing ( */
	{ "a", "(body-contains \"data2\")", "move-to-folder \"folder2\")" }, /* missing ( */
	{ "a", "(body-contains \"data2\")", "(move-to-folder \"folder2\" ())" }, /* invalid () */
	{ "a", "(body-contains \"data2\")", "()" }, /* invalid () */
	{ "a", "(body-contains \"data2\")", "" }, /* empty */
};

static CamelFolder *
get_folder (CamelFilterDriver *d,
            const gchar *uri,
            gpointer data,
            GError **error)
{
	gint i;

	for (i = 0; i < G_N_ELEMENTS (mailboxes); i++)
		if (!strcmp (mailboxes[i].name, uri)) {
			return g_object_ref (mailboxes[i].folder);
		}
	return NULL;
}

static void
test_simple_filtering (void)
{
	CamelService *service;
	CamelSession *session;
	CamelStore *store;
	CamelFolder *folder;
	CamelMimeMessage *msg;
	CamelStream *mbox;
	CamelFilterDriver *driver;
	gchar *store_uri;
	gchar *inbox_path;
	gint i, j;
	GError *error = NULL;


	session = camel_test_session_new (camel_test_get_dir ());

	/* todo: cross-check everything with folder_info checks as well */
	/* todo: work out how to do imap/pop/nntp tests */

	store_uri = g_strdup_printf ("mbox:///%s/mbox", camel_test_get_dir ());
	service = camel_session_add_service (
		session, "test-uid", store_uri,
		CAMEL_PROVIDER_STORE, &error);
	g_free (store_uri);
	g_assert_no_error (error);
	g_assert_true (CAMEL_IS_STORE (service));
	store = CAMEL_STORE (service);

	for (i = 0; i < G_N_ELEMENTS (mailboxes); i++) {
		mailboxes[i].folder = folder = camel_store_get_folder_sync (
			store, mailboxes[i].name,
			CAMEL_STORE_FOLDER_CREATE, NULL, &error);
		g_assert_no_error (error);
		g_assert_nonnull (folder);

		/* we need an empty folder for this to work */
		test_folder_counts (folder, 0, 0);
	}

	/* append a bunch of messages with specific content */
	inbox_path = g_strdup_printf ("%s/inbox", camel_test_get_dir ());
	mbox = camel_stream_fs_new_with_name (inbox_path, O_WRONLY | O_CREAT | O_EXCL, 0600, NULL);
	for (j = 0; j < 100; j++) {
		gchar *content, *subject;

		msg = test_message_create_simple ();
		content = g_strdup_printf ("data%d content\n", j);
		test_message_set_content_simple (
			(CamelMimePart *) msg, 0, "text/plain",
						content, strlen (content));
		g_free (content);
		subject = g_strdup_printf ("Test%d message%d subject", j, 100 - j);
		camel_mime_message_set_subject (msg, subject);

		camel_mime_message_set_date (msg, j * 60 * 24, 0);

		g_warn_if_fail (camel_stream_write_string (mbox, "From \n", NULL, NULL));
		g_assert_true (camel_data_wrapper_write_to_stream_sync (
			CAMEL_DATA_WRAPPER (msg), mbox, NULL, NULL) != -1);

		g_free (subject);

		g_assert_cmpuint (G_OBJECT (msg)->ref_count, ==, 1);
		g_clear_object (&msg);
	}
	g_assert_true (camel_stream_close (mbox, NULL, NULL) != -1);
	g_assert_cmpuint (G_OBJECT (mbox)->ref_count, ==, 1);
	g_clear_object (&mbox);

	driver = camel_filter_driver_new (session);
	camel_filter_driver_set_folder_func (driver, get_folder, NULL);
	for (i = 0; i < G_N_ELEMENTS (rules); i++) {
		camel_filter_driver_add_rule (driver, rules[i].name, rules[i].match, rules[i].action);
	}

	camel_filter_driver_set_default_folder (driver, mailboxes[0].folder);
#if 0  /* FIXME We no longer filter mbox files. */
	camel_filter_driver_filter_mbox (
		driver, inbox_path, NULL, NULL, &error);
#endif
	g_assert_no_error (error);

	/* now need to check the folder counts/etc */

	g_assert_cmpuint (G_OBJECT (driver)->ref_count, ==, 1);
	g_clear_object (&driver);

	/* this tests that invalid rules are caught */
	for (i = 0; i < G_N_ELEMENTS (brokens); i++) {
		error = NULL;
		driver = camel_filter_driver_new (session);
		camel_filter_driver_set_folder_func (driver, get_folder, NULL);
		camel_filter_driver_add_rule (driver, brokens[i].name, brokens[i].match, brokens[i].action);
#if 0  /* FIXME We no longer filter mbox files. */
		camel_filter_driver_filter_mbox (
			driver, inbox_path, NULL, NULL, &error);
#endif
		g_assert_nonnull (error);
		g_assert_cmpuint (G_OBJECT (driver)->ref_count, ==, 1);
		g_clear_object (&driver);
		g_clear_error (&error);
	}

	for (i = 0; i < G_N_ELEMENTS (brokena); i++) {
		error = NULL;
		driver = camel_filter_driver_new (session);
		camel_filter_driver_set_folder_func (driver, get_folder, NULL);
		camel_filter_driver_add_rule (driver, brokena[i].name, brokena[i].match, brokena[i].action);
#if 0  /* FIXME We no longer filter mbox files. */
		camel_filter_driver_filter_mbox (
			driver, inbox_path, NULL, NULL, &error);
#endif
		g_assert_nonnull (error);
		g_assert_cmpuint (G_OBJECT (driver)->ref_count, ==, 1);
		g_clear_object (&driver);
		g_clear_error (&error);
	}

	for (i = 0; i < G_N_ELEMENTS (mailboxes); i++) {
		g_assert_cmpuint (G_OBJECT (mailboxes[i].folder)->ref_count, ==, 1);
		g_clear_object (&mailboxes[i].folder);
	}

	g_assert_cmpuint (G_OBJECT (store)->ref_count, ==, 1);
	g_clear_object (&store);

	g_free (inbox_path);
	g_clear_object (&session);
}

gint
main (gint argc,
      gchar **argv)
{
	gint ret;

	camel_test_init (&argc, &argv);
	camel_test_provider_init (1, local_drivers);

	g_test_add_func ("/Camel/Folder/Index/simple-filtering", test_simple_filtering);

	ret = g_test_run ();
	camel_test_shutdown ();
	return ret;
}
