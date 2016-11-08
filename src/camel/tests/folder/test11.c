/*
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
	check_msg (folders->len == len, "unexpected number of folders returned from folderinfo");
	qsort (folders->pdata, folders->len, sizeof (folders->pdata[0]), cmp_fi);
	for (i = 0; i < len; i++) {
		CamelFolderInfo *f = folders->pdata[i];

		camel_test_push ("checking folder '%s'", list[i].display_name);

		check (!strcmp (f->full_name, list[i].full_name));

		/* this might be translated, but we can't know */
		camel_test_nonfatal ("Inbox not english");
		check (!strcmp (f->display_name, list[i].display_name));
		camel_test_fatal ();

		camel_test_nonfatal ("Flags mismatch");
		check (f->flags == list[i].flags);
		camel_test_fatal ();

		camel_test_pull ();
	}

	g_ptr_array_free (folders, TRUE);
}

gint
main (gint argc,
      gchar **argv)
{
	CamelFolder *f1, *f2;
	CamelStore *store;
	CamelService *service;
	CamelFolderInfo *fi;
	GError *error = NULL;

	camel_test_init (argc, argv);
	camel_test_provider_init (1, local_drivers);

	/* clear out any camel-test data */
	system ("/bin/rm -rf /tmp/camel-test");

	session = camel_test_session_new ("/tmp/camel-test");
	service = camel_session_add_service (
		session, "test-uid",
		"maildir:///tmp/camel-test/maildir",
		CAMEL_PROVIDER_STORE, NULL);
	store = CAMEL_STORE (service);

	camel_test_start ("Maildir backward compatability tests");

	camel_test_push ("./ prefix path, one level");
	f1 = camel_store_get_folder_sync (
		store, "testbox",
		CAMEL_STORE_FOLDER_CREATE, NULL, &error);
	check_msg (error == NULL, "%s", error->message);
	g_clear_error (&error);
	f2 = camel_store_get_folder_sync (
		store, "./testbox",
		CAMEL_STORE_FOLDER_CREATE, NULL, &error);
	check_msg (error == NULL, "%s", error->message);
	g_clear_error (&error);
	check (f1 == f2);
	check_unref (f2, 2);
	check_unref (f1, 1);
	camel_test_pull ();

	camel_test_push ("./ prefix path, one level, no create");
	f1 = camel_store_get_folder_sync (
		store, "testbox2",
		CAMEL_STORE_FOLDER_CREATE, NULL, &error);
	check_msg (error == NULL, "%s", error->message);
	g_clear_error (&error);
	f2 = camel_store_get_folder_sync (
		store, "./testbox2", 0, NULL, &error);
	check_msg (error == NULL, "%s", error->message);
	g_clear_error (&error);
	check (f1 == f2);
	check_unref (f2, 2);
	check_unref (f1, 1);
	camel_test_pull ();

	camel_test_push ("./ prefix path, two levels");
	f1 = camel_store_get_folder_sync (
		store, "testbox/foo",
		CAMEL_STORE_FOLDER_CREATE, NULL, &error);
	check_msg (error == NULL, "%s", error->message);
	g_clear_error (&error);
	f2 = camel_store_get_folder_sync (
		store, "./testbox/foo",
		CAMEL_STORE_FOLDER_CREATE, NULL, &error);
	check_msg (error == NULL, "%s", error->message);
	g_clear_error (&error);
	check (f1 == f2);
	check_unref (f2, 2);
	check_unref (f1, 1);
	camel_test_pull ();

	camel_test_push ("'.' == Inbox");
	f2 = camel_store_get_inbox_folder_sync (store, NULL, &error);
	check_msg (error == NULL, "%s", error->message);
	g_clear_error (&error);
	f1 = camel_store_get_folder_sync (store, ".", 0, NULL, &error);
	check_msg (error == NULL, "%s", error->message);
	g_clear_error (&error);
	check (f1 == f2);
	check_unref (f2, 2);
	check_unref (f1, 1);
	camel_test_pull ();

	camel_test_push ("folder info, recursive");
	fi = camel_store_get_folder_info_sync (
		store, "",
		CAMEL_STORE_FOLDER_INFO_RECURSIVE,
		NULL, &error);
	check_msg (error == NULL, "%s", error->message);
	g_clear_error (&error);
	check (fi != NULL);
	check_fi (fi, fi_list_1, G_N_ELEMENTS (fi_list_1));
	camel_test_pull ();

	camel_test_push ("folder info, flat");
	fi = camel_store_get_folder_info_sync (store, "", 0, NULL, &error);
	check_msg (error == NULL, "%s", error->message);
	g_clear_error (&error);
	check (fi != NULL);
	check_fi (fi, fi_list_2, G_N_ELEMENTS (fi_list_2));
	camel_test_pull ();

	camel_test_push ("folder info, recursive, non root");
	fi = camel_store_get_folder_info_sync (
		store, "testbox",
		CAMEL_STORE_FOLDER_INFO_RECURSIVE,
		NULL, &error);
	check_msg (error == NULL, "%s", error->message);
	g_clear_error (&error);
	check (fi != NULL);
	check_fi (fi, fi_list_3, G_N_ELEMENTS (fi_list_3));
	camel_test_pull ();

	check_unref (store, 1);
	check_unref (session, 1);

	camel_test_end ();

	return 0;
}
