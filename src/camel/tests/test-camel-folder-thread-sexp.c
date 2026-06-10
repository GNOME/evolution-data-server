/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

/* threaded folder testing */

#include <string.h>

#include "camel-test.h"
#include "camel-test-provider.h"
#include "session.h"

#define MAX_LOOP (10000)
#define MAX_THREADS (5)
#define GC(x) ((gchar *) (x))

static const gchar *local_drivers[] = { "local" };

static CamelSession *session;

/* FIXME: flags aren't really right yet */
/* ASCII sorted on full_name */
static CamelFolderInfo fi_list_1[] = {
	{ NULL, NULL, NULL, GC ("."), GC ("Inbox"), CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_NOCHILDREN, -1, -1 },
	{ NULL, NULL, NULL, GC (".#evolution/Junk"), GC ("Junk"), CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_NOCHILDREN, -1, -1 },
	{ NULL, NULL, NULL, GC (".#evolution/Trash"), GC ("Trash"), CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_NOCHILDREN, -1, -1 },
	{ NULL, NULL, NULL, GC ("testbox"), GC ("testbox"), CAMEL_FOLDER_CHILDREN, -1, -1 },
	{ NULL, NULL, NULL, GC ("testbox/foo"), GC ("foo"), CAMEL_FOLDER_NOCHILDREN, -1, -1 },
	{ NULL, NULL, NULL, GC ("testbox2"), GC ("testbox2"), CAMEL_FOLDER_NOCHILDREN, -1, -1 },
};

static CamelFolderInfo fi_list_2[] = {
	{ NULL, NULL, NULL, GC ("."), GC ("Inbox"), CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_NOCHILDREN, -1, -1 },
	{ NULL, NULL, NULL, GC (".#evolution/Junk"), GC ("Junk"), CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_NOCHILDREN, -1, -1 },
	{ NULL, NULL, NULL, GC (".#evolution/Trash"), GC ("Trash"), CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_NOCHILDREN, -1, -1 },
	{ NULL, NULL, NULL, GC ("testbox"), GC ("testbox"), CAMEL_FOLDER_NOCHILDREN, -1, -1 },
	{ NULL, NULL, NULL, GC ("testbox2"), GC ("testbox2"), CAMEL_FOLDER_NOCHILDREN, -1, -1 },
};

static CamelFolderInfo fi_list_3[] = {
	{ NULL, NULL, NULL, GC ("testbox"), GC ("testbox"), CAMEL_FOLDER_CHILDREN, -1, -1 },
	{ NULL, NULL, NULL, GC ("testbox/foo"), GC ("foo"), CAMEL_FOLDER_NOCHILDREN, -1, -1 },
};

static gint
cmp_fi (gconstpointer a,
        gconstpointer b)
{
	const CamelFolderInfo *fa = ((const CamelFolderInfo **) a)[0];
	const CamelFolderInfo *fb = ((const CamelFolderInfo **) b)[0];

	return strcmp (fa->full_name, fb->full_name);
}

static void
add_fi (GPtrArray *folders,
        CamelFolderInfo *fi)
{
	while (fi) {
		g_ptr_array_add (folders, fi);
		if (fi->child)
			add_fi (folders, fi->child);
		fi = fi->next;
	}
}

static void
check_fi (CamelFolderInfo *fi,
          CamelFolderInfo *list,
          gint len)
{
	GPtrArray *folders = g_ptr_array_new ();
	gint i;

	add_fi (folders, fi);
	g_assert_cmpuint (folders->len, ==, len);
	qsort (folders->pdata, folders->len, sizeof (folders->pdata[0]), cmp_fi);
	for (i = 0; i < len; i++) {
		CamelFolderInfo *f = folders->pdata[i];

		g_assert_cmpstr (f->full_name, ==, list[i].full_name);

		/* this might be translated, but we can't know */
		/* g_assert_cmpstr (f->display_name, ==, list[i].display_name); */

		/* g_assert_cmpuint (f->flags, ==, list[i].flags); */
	}

	g_ptr_array_free (folders, TRUE);
}

static void
test_prefix_path_one_level (void)
{
	CamelFolder *f1, *f2;
	CamelStore *store;
	CamelService *service;
	gchar *store_uri;
	GError *error = NULL;


	session = camel_test_session_new (camel_test_get_dir ());
	store_uri = g_strdup_printf ("maildir:///%s/maildir", camel_test_get_dir ());
	service = camel_session_add_service (
		session, "test-uid",
		store_uri,
		CAMEL_PROVIDER_STORE, NULL);
	g_free (store_uri);
	store = CAMEL_STORE (service);

	f1 = camel_store_get_folder_sync (
		store, "testbox",
		CAMEL_STORE_FOLDER_CREATE, NULL, &error);
	g_assert_no_error (error);
	f2 = camel_store_get_folder_sync (
		store, "./testbox",
		CAMEL_STORE_FOLDER_CREATE, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (f1 == f2);
	g_assert_cmpuint (G_OBJECT (f2)->ref_count, ==, 2);
	g_object_unref (f2);
	g_assert_cmpuint (G_OBJECT (f1)->ref_count, ==, 1);
	g_clear_object (&f1);

	g_assert_cmpuint (G_OBJECT (store)->ref_count, ==, 1);
	g_clear_object (&store);
	g_clear_object (&session);
}

static void
test_prefix_path_one_level_no_create (void)
{
	CamelFolder *f1, *f2;
	CamelStore *store;
	CamelService *service;
	gchar *store_uri;
	GError *error = NULL;


	session = camel_test_session_new (camel_test_get_dir ());
	store_uri = g_strdup_printf ("maildir:///%s/maildir", camel_test_get_dir ());
	service = camel_session_add_service (
		session, "test-uid",
		store_uri,
		CAMEL_PROVIDER_STORE, NULL);
	g_free (store_uri);
	store = CAMEL_STORE (service);

	f1 = camel_store_get_folder_sync (
		store, "testbox2",
		CAMEL_STORE_FOLDER_CREATE, NULL, &error);
	g_assert_no_error (error);
	f2 = camel_store_get_folder_sync (
		store, "./testbox2", 0, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (f1 == f2);
	g_assert_cmpuint (G_OBJECT (f2)->ref_count, ==, 2);
	g_object_unref (f2);
	g_assert_cmpuint (G_OBJECT (f1)->ref_count, ==, 1);
	g_clear_object (&f1);

	g_assert_cmpuint (G_OBJECT (store)->ref_count, ==, 1);
	g_clear_object (&store);
	g_clear_object (&session);
}

static void
test_prefix_path_two_levels (void)
{
	CamelFolder *f1, *f2;
	CamelStore *store;
	CamelService *service;
	gchar *store_uri;
	GError *error = NULL;


	session = camel_test_session_new (camel_test_get_dir ());
	store_uri = g_strdup_printf ("maildir:///%s/maildir", camel_test_get_dir ());
	service = camel_session_add_service (
		session, "test-uid",
		store_uri,
		CAMEL_PROVIDER_STORE, NULL);
	g_free (store_uri);
	store = CAMEL_STORE (service);

	/* Need testbox to exist first for the subfolder */
	f1 = camel_store_get_folder_sync (
		store, "testbox",
		CAMEL_STORE_FOLDER_CREATE, NULL, &error);
	g_assert_no_error (error);
	g_clear_object (&f1);

	f1 = camel_store_get_folder_sync (
		store, "testbox/foo",
		CAMEL_STORE_FOLDER_CREATE, NULL, &error);
	g_assert_no_error (error);
	f2 = camel_store_get_folder_sync (
		store, "./testbox/foo",
		CAMEL_STORE_FOLDER_CREATE, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (f1 == f2);
	g_assert_cmpuint (G_OBJECT (f2)->ref_count, ==, 2);
	g_object_unref (f2);
	g_assert_cmpuint (G_OBJECT (f1)->ref_count, ==, 1);
	g_clear_object (&f1);

	g_assert_cmpuint (G_OBJECT (store)->ref_count, ==, 1);
	g_clear_object (&store);
	g_clear_object (&session);
}

static void
test_dot_is_inbox (void)
{
	CamelFolder *f1, *f2;
	CamelStore *store;
	CamelService *service;
	gchar *store_uri;
	GError *error = NULL;


	session = camel_test_session_new (camel_test_get_dir ());
	store_uri = g_strdup_printf ("maildir:///%s/maildir", camel_test_get_dir ());
	service = camel_session_add_service (
		session, "test-uid",
		store_uri,
		CAMEL_PROVIDER_STORE, NULL);
	g_free (store_uri);
	store = CAMEL_STORE (service);

	f2 = camel_store_get_inbox_folder_sync (store, NULL, &error);
	g_assert_no_error (error);
	f1 = camel_store_get_folder_sync (store, ".", 0, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (f1 == f2);
	g_assert_cmpuint (G_OBJECT (f2)->ref_count, ==, 2);
	g_object_unref (f2);
	g_assert_cmpuint (G_OBJECT (f1)->ref_count, ==, 1);
	g_clear_object (&f1);

	g_assert_cmpuint (G_OBJECT (store)->ref_count, ==, 1);
	g_clear_object (&store);
	g_clear_object (&session);
}

static void
test_folder_info_recursive (void)
{
	CamelFolder *f1;
	CamelStore *store;
	CamelService *service;
	CamelFolderInfo *fi;
	gchar *store_uri;
	GError *error = NULL;


	session = camel_test_session_new (camel_test_get_dir ());
	store_uri = g_strdup_printf ("maildir:///%s/maildir", camel_test_get_dir ());
	service = camel_session_add_service (
		session, "test-uid",
		store_uri,
		CAMEL_PROVIDER_STORE, NULL);
	g_free (store_uri);
	store = CAMEL_STORE (service);

	/* Create the folders first */
	f1 = camel_store_get_folder_sync (
		store, "testbox",
		CAMEL_STORE_FOLDER_CREATE, NULL, &error);
	g_assert_no_error (error);
	g_clear_object (&f1);

	f1 = camel_store_get_folder_sync (
		store, "testbox/foo",
		CAMEL_STORE_FOLDER_CREATE, NULL, &error);
	g_assert_no_error (error);
	g_clear_object (&f1);

	f1 = camel_store_get_folder_sync (
		store, "testbox2",
		CAMEL_STORE_FOLDER_CREATE, NULL, &error);
	g_assert_no_error (error);
	g_clear_object (&f1);

	fi = camel_store_get_folder_info_sync (
		store, "",
		CAMEL_STORE_FOLDER_INFO_RECURSIVE,
		NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (fi);
	check_fi (fi, fi_list_1, G_N_ELEMENTS (fi_list_1));

	g_assert_cmpuint (G_OBJECT (store)->ref_count, ==, 1);
	g_clear_object (&store);
	g_clear_object (&session);
}

static void
test_folder_info_flat (void)
{
	CamelFolder *f1;
	CamelStore *store;
	CamelService *service;
	CamelFolderInfo *fi;
	gchar *store_uri;
	GError *error = NULL;


	session = camel_test_session_new (camel_test_get_dir ());
	store_uri = g_strdup_printf ("maildir:///%s/maildir", camel_test_get_dir ());
	service = camel_session_add_service (
		session, "test-uid",
		store_uri,
		CAMEL_PROVIDER_STORE, NULL);
	g_free (store_uri);
	store = CAMEL_STORE (service);

	/* Create the folders first */
	f1 = camel_store_get_folder_sync (
		store, "testbox",
		CAMEL_STORE_FOLDER_CREATE, NULL, &error);
	g_assert_no_error (error);
	g_clear_object (&f1);

	f1 = camel_store_get_folder_sync (
		store, "testbox/foo",
		CAMEL_STORE_FOLDER_CREATE, NULL, &error);
	g_assert_no_error (error);
	g_clear_object (&f1);

	f1 = camel_store_get_folder_sync (
		store, "testbox2",
		CAMEL_STORE_FOLDER_CREATE, NULL, &error);
	g_assert_no_error (error);
	g_clear_object (&f1);

	fi = camel_store_get_folder_info_sync (store, "", 0, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (fi);
	check_fi (fi, fi_list_2, G_N_ELEMENTS (fi_list_2));

	g_assert_cmpuint (G_OBJECT (store)->ref_count, ==, 1);
	g_clear_object (&store);
	g_clear_object (&session);
}

static void
test_folder_info_recursive_non_root (void)
{
	CamelFolder *f1;
	CamelStore *store;
	CamelService *service;
	CamelFolderInfo *fi;
	gchar *store_uri;
	GError *error = NULL;


	session = camel_test_session_new (camel_test_get_dir ());
	store_uri = g_strdup_printf ("maildir:///%s/maildir", camel_test_get_dir ());
	service = camel_session_add_service (
		session, "test-uid",
		store_uri,
		CAMEL_PROVIDER_STORE, NULL);
	g_free (store_uri);
	store = CAMEL_STORE (service);

	/* Create the folders first */
	f1 = camel_store_get_folder_sync (
		store, "testbox",
		CAMEL_STORE_FOLDER_CREATE, NULL, &error);
	g_assert_no_error (error);
	g_clear_object (&f1);

	f1 = camel_store_get_folder_sync (
		store, "testbox/foo",
		CAMEL_STORE_FOLDER_CREATE, NULL, &error);
	g_assert_no_error (error);
	g_clear_object (&f1);

	f1 = camel_store_get_folder_sync (
		store, "testbox2",
		CAMEL_STORE_FOLDER_CREATE, NULL, &error);
	g_assert_no_error (error);
	g_clear_object (&f1);

	fi = camel_store_get_folder_info_sync (
		store, "testbox",
		CAMEL_STORE_FOLDER_INFO_RECURSIVE,
		NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (fi);
	check_fi (fi, fi_list_3, G_N_ELEMENTS (fi_list_3));

	g_assert_cmpuint (G_OBJECT (store)->ref_count, ==, 1);
	g_clear_object (&store);
	g_clear_object (&session);
}

gint
main (gint argc,
      gchar **argv)
{
	gint ret;

	camel_test_init (&argc, &argv);
	camel_test_provider_init (1, local_drivers);

	g_test_add_func ("/Camel/Folder/ThreadSexp/prefix-path-one-level", test_prefix_path_one_level);
	g_test_add_func ("/Camel/Folder/ThreadSexp/prefix-path-one-level-no-create", test_prefix_path_one_level_no_create);
	g_test_add_func ("/Camel/Folder/ThreadSexp/prefix-path-two-levels", test_prefix_path_two_levels);
	g_test_add_func ("/Camel/Folder/ThreadSexp/dot-is-inbox", test_dot_is_inbox);
	g_test_add_func ("/Camel/Folder/ThreadSexp/folder-info-recursive", test_folder_info_recursive);
	g_test_add_func ("/Camel/Folder/ThreadSexp/folder-info-flat", test_folder_info_flat);
	g_test_add_func ("/Camel/Folder/ThreadSexp/folder-info-recursive-non-root", test_folder_info_recursive_non_root);

	ret = g_test_run ();
	camel_test_shutdown ();
	return ret;
}
