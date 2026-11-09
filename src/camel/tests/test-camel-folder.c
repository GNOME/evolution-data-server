/*
 * SPDX-FileCopyrightText: (C) 2026 Tobias Mueller <muelli@cryptobitch.de>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "evolution-data-server-config.h"

#include "camel-test.h"

#define PROXY_TYPE_FOLDER (proxy_folder_get_type ())
G_DECLARE_FINAL_TYPE (ProxyFolder, proxy_folder, PROXY, FOLDER, CamelFolder)

struct _ProxyFolder {
	CamelFolder parent_instance;

	GCancellable *caller_cancellable;
	gboolean proxy_finalized;
};

G_DEFINE_TYPE (ProxyFolder, proxy_folder, CAMEL_TYPE_FOLDER)

static void
proxy_finalized_cb (gpointer user_data,
		    GObject *where_the_proxy_was)
{
	ProxyFolder *self = user_data;

	/* only compares the pointer, the proxy is freed */
	g_assert_cmpuint (g_signal_handler_find (self->caller_cancellable, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, where_the_proxy_was), ==, 0);

	self->proxy_finalized = TRUE;
}

static CamelMimeMessage *
proxy_folder_get_message_sync (CamelFolder *folder,
			       const gchar *message_uid,
			       GCancellable *cancellable,
			       GError **error)
{
	ProxyFolder *self = PROXY_FOLDER (folder);

	/* the default transfer_messages_to_sync() passes its proxy cancellable */
	g_assert_true (G_IS_CANCELLABLE (cancellable));
	g_assert_true (cancellable != self->caller_cancellable);

	g_object_weak_ref (G_OBJECT (cancellable), proxy_finalized_cb, self);

	g_set_error_literal (error, CAMEL_ERROR, CAMEL_ERROR_GENERIC, "No message");

	return NULL;
}

static void
proxy_folder_class_init (ProxyFolderClass *klass)
{
	CamelFolderClass *folder_class;

	folder_class = CAMEL_FOLDER_CLASS (klass);
	folder_class->get_message_sync = proxy_folder_get_message_sync;
}

static void
proxy_folder_init (ProxyFolder *self)
{
}

static void
test_transfer_proxy_cancellable (void)
{
	CamelStore *store;
	CamelFolder *source, *destination;
	GCancellable *cancellable;
	GPtrArray *uids;
	GError *local_error = NULL;

	store = test_store_new ();
	source = g_object_new (PROXY_TYPE_FOLDER,
		"parent-store", store,
		"full-name", "source",
		"display-name", "source",
		NULL);
	destination = test_folder_new (store, "destination");

	cancellable = g_cancellable_new ();
	PROXY_FOLDER (source)->caller_cancellable = cancellable;

	uids = g_ptr_array_new ();
	g_ptr_array_add (uids, (gpointer) "1");

	camel_folder_transfer_messages_to_sync (source, uids, destination, FALSE, NULL, cancellable, &local_error);
	g_assert_error (local_error, CAMEL_ERROR, CAMEL_ERROR_GENERIC);
	g_clear_error (&local_error);

	g_assert_true (PROXY_FOLDER (source)->proxy_finalized);

	g_ptr_array_unref (uids);
	g_object_unref (cancellable);
	g_object_unref (destination);
	g_object_unref (source);
	g_object_unref (store);
}

gint
main (gint argc,
      gchar **argv)
{
	gint ret;

	camel_test_init (&argc, &argv);

	g_test_add_func ("/Camel/Folder/TransferProxyCancellable", test_transfer_proxy_cancellable);

	ret = g_test_run ();
	camel_test_shutdown ();

	return ret;
}
