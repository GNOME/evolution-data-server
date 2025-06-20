/*
 * SPDX-FileCopyrightText: (C) 2025 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/* include as #include "lib-camel-test-utils.c" after camel.h */

#include <glib/gstdio.h>

#define DELAY_TIMEOUT_SECONDS 5

#define TEST_TYPE_SESSION (test_session_get_type ())
G_DECLARE_FINAL_TYPE (TestSession, test_session, TEST, SESSION, CamelSession)

static gint n_alive_test_sessions = 0;

struct _TestSession {
	CamelSession parent_instance;

	guint32 n_called_addressbook_contains;
	guint32 n_pending_jobs;
};

G_DEFINE_TYPE (TestSession, test_session, CAMEL_TYPE_SESSION)

static void
test_session_job_started_cb (CamelSession *session)
{
	TestSession *self = TEST_SESSION (session);

	self->n_pending_jobs++;
}

static void
test_session_job_finished_cb (CamelSession *session)
{
	TestSession *self = TEST_SESSION (session);

	g_assert_cmpuint (self->n_pending_jobs, >, 0);
	self->n_pending_jobs--;
}

static gboolean
test_session_addressbook_contains_sync (CamelSession *session,
					const gchar *book_uid,
					const gchar *email_address,
					GCancellable *cancellable,
					GError **error)
{
	TestSession *self;

	g_assert_true (TEST_IS_SESSION (session));
	g_assert_nonnull (book_uid);
	g_assert_nonnull (email_address);

	self = TEST_SESSION (session);
	self->n_called_addressbook_contains++;

	if (g_str_equal (book_uid, "book1"))
		return camel_strstrcase (email_address, "bruce@no.where") != NULL ||
			camel_strstrcase (email_address, "tony@no.where") != NULL;

	if (g_str_equal (book_uid, "book2"))
		return camel_strstrcase (email_address, "gwen@no.where") != NULL;

	return FALSE;
}

static GObject *
test_session_constructor (GType type,
			  guint n_construct_params,
			  GObjectConstructParam *construct_params)
{
	static GObject *singleton = NULL;
	GObject *object;

	if (singleton) {
		object = g_object_ref (singleton);
	} else {
		object = G_OBJECT_CLASS (test_session_parent_class)->constructor (type, n_construct_params, construct_params);

		if (object)
			g_object_weak_ref (object, (GWeakNotify) g_nullify_pointer, &singleton);

		singleton = object;
	}

	return object;
}

static void
test_session_finalize (GObject *object)
{
	TestSession *self = TEST_SESSION (object);

	g_assert_cmpuint (self->n_pending_jobs, ==, 0);
	g_assert_cmpint (n_alive_test_sessions, >, 0);

	g_atomic_int_add (&n_alive_test_sessions, -1);

	G_OBJECT_CLASS (test_session_parent_class)->finalize (object);
}

static void
test_session_class_init (TestSessionClass *klass)
{
	GObjectClass *object_class;
	CamelSessionClass *session_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->constructor = test_session_constructor;
	object_class->finalize = test_session_finalize;

	session_class = CAMEL_SESSION_CLASS (klass);
	session_class->addressbook_contains_sync = test_session_addressbook_contains_sync;
}

static void
test_session_init (TestSession *self)
{
	g_atomic_int_add (&n_alive_test_sessions, 1);
	g_signal_connect (self, "job-started", G_CALLBACK (test_session_job_started_cb), NULL);
	g_signal_connect (self, "job-finished", G_CALLBACK (test_session_job_finished_cb), NULL);
}

static CamelSession *
test_session_new (void)
{
	CamelSession *session;
	gchar *data_dir, *cache_dir;

	data_dir = g_build_filename (g_get_tmp_dir (), "camel-test", "data-dir", NULL);
	cache_dir = g_build_filename (g_get_tmp_dir (), "camel-test", "cache-dir", NULL);

	session = g_object_new (TEST_TYPE_SESSION,
		"online", TRUE,
		"user-data-dir", data_dir,
		"user-cache-dir", cache_dir,
		NULL);

	g_free (data_dir);
	g_free (cache_dir);

	return session;
}

static void
test_session_check_finalized (void)
{
	g_assert_cmpint (n_alive_test_sessions, ==, 0);
}

static gboolean
test_util_abort_on_timeout_cb (gpointer user_data)
{
	g_assert_not_reached ();
	return G_SOURCE_REMOVE;
}

static gboolean
test_util_reset_idle_timer (gpointer user_data)
{
	guint *ptimeout_id = user_data;
	*ptimeout_id = 0;
	return G_SOURCE_REMOVE;
}

static void
test_session_wait_for_pending_jobs (void)
{
	TestSession *test_session;
	guint timeout_id;

	/* it's a singleton, thus it's okay to reach it this way */
	test_session = TEST_SESSION (test_session_new ());

	/* jobs are scheduled in an idle callback, which might not be called yet, thus
	   wait for up to 100ms in case there's anything pending */

	if (!test_session->n_pending_jobs) {
		timeout_id = g_timeout_add (100, test_util_reset_idle_timer, &timeout_id);
		g_assert_cmpuint (timeout_id, !=, 0);

		while (!test_session->n_pending_jobs && timeout_id) {
			g_main_context_iteration (NULL, TRUE);
		}

		if (timeout_id != 0)
			g_assert_true (g_source_remove (timeout_id));
	}

	if (test_session->n_pending_jobs > 0) {
		timeout_id = g_timeout_add_seconds (DELAY_TIMEOUT_SECONDS, test_util_abort_on_timeout_cb, NULL);
		g_assert_cmpuint (timeout_id, !=, 0);

		while (test_session->n_pending_jobs > 0) {
			g_main_context_iteration (NULL, TRUE);
		}

		g_assert_true (g_source_remove (timeout_id));
	}

	g_clear_object (&test_session);
}

static void
test_store_search_read_message_data (const gchar *uid,
				     CamelMessageInfo *inout_nfo,
				     CamelMimeMessage **out_message)
{
	struct _data {
		const gchar *uid;
		const gchar *mime;
	} data[] = {
		{ "11",
		  "Message-ID: <11@no.where>\r\n"
		  "Subject: With UTF-8\r\n"
		  "From: =?UTF-8?Q?Tom=C3=A1=C5=A1?= <tom@no.where>\r\n"
		  "To: user@no.where\r\n"
		  "Date: Thu, 15 May 2025 12:45:14 +0000\r\n"
		  "Content-Type: text/plain; charset=\"UTF-8\"\r\n"
		  "Content-Transfer-Encoding: quoted-printable\r\n"
		  "MIME-Version: 1.0\r\n"
		  "\r\n"
		  "Body text with longer line\r\n"
		  "in bla msg=31=31 bla\r\n" },
		{ "12",
		  "Message-ID: <12@no.where>\r\n"
		  "Subject: alb alb\r\n"
		  "From: user1 <user1@no.where>\r\n"
		  "To: user2@no.where\r\n"
		  "Bcc: user3@no.where\r\n"
		  "Date: Mon, 12 May 2025 07:15:00 +0000\r\n"
		  "Content-Type: text/plain; charset=\"UTF-8\"\r\n"
		  "Content-Transfer-Encoding: quoted-printable\r\n"
		  "MIME-Version: 1.0\r\n"
		  "\r\n"
		  "Test text bla bla\r\n"
		  "blur bla bla\r\n"
		  "\r\n" },
		{ "13",
		  "Message-ID: <13@no.where>\r\n"
		  "Subject: Message 13\r\n"
		  "From: user3@no.where\r\n"
		  "Cc: user3@no.where\r\n"
		  "Bcc: =?UTF-8?Q?Tom=C3=A1=C5=A1?= <tom@no.where>\r\n"
		  "Date: Mon, 12 May 2025 07:15:00 +0000\r\n"
		  "Received: from [192.168.1.2] by 127.0.0.1 with ESMTPA Tue, 13 May 2025 23:50:00 +0000\r\n"
		  "Content-Type: text/plain; charset=\"UTF-8\"\r\n"
		  "Content-Transfer-Encoding: quoted-printable\r\n"
		  "MIME-Version: 1.0\r\n"
		  "\r\n"
		  "Text in message 13\r\n"
		  "\r\n" },
		{ "21",
		  "Message-ID: <21@no.where>\r\n"
		  "Subject: 21st Message\r\n"
		  "From: user2@no.where\r\n"
		  "Date: Tue, 13 May 2025 13:13:13 +0000\r\n"
		  "Content-Type: text/plain; charset=\"UTF-8\"\r\n"
		  "Content-Transfer-Encoding: quoted-printable\r\n"
		  "MIME-Version: 1.0\r\n"
		  "\r\n"
		  "Interesting poem here and there on Tuesday May 13th\r\n"
		  "without To and without CC\r\n" },
		{ "22",
		  "Message-ID: <22@no.where>\r\n"
		  "Subject: forecast\r\n"
		  "From: user5@no.where\r\n"
		  "To: user2@no.where\r\n"
		  "Date: Mon, 12 May 2025 08:15:00 +0000\r\n"
		  "Content-Type: text/plain; charset=\"UTF-8\"\r\n"
		  "Content-Transfer-Encoding: quoted-printable\r\n"
		  "MIME-Version: 1.0\r\n"
		  "\r\n"
		  "Expect sunny day, mostly\r\n"
		  "\r\n" },
		{ "23",
		  "Message-ID: <23@no.where>\r\n"
		  "Subject: Re: forecast\r\n"
		  "From: user <user@no.where>\r\n"
		  "To: user2@no.where\r\n"
		  "In-Reply-To: <23@no.where>\r\n"
		  "Date: Wed, 14 May 2025 10:00:00 +0000\r\n"
		  "Content-Type: text/plain; charset=\"UTF-8\"\r\n"
		  "Content-Transfer-Encoding: quoted-printable\r\n"
		  "X-Custom-Header: value A\r\n"
		  "MIME-Version: 1.0\r\n"
		  "\r\n"
		  "The weather will change today;\r\n"
		  "Have no expectations... maybe sunny... maybe mostly cloudy...\r\n"
		  "\r\n" },
		{ "31",
		  "Message-ID: <31@no.where>\r\n"
		  "Subject: Thirty first message\r\n"
		  "From: Some User <some@no.where>\r\n"
		  "To: user@no.where\r\n"
		  "Date: Tue, 13 May 2025 12:00:00 +0000\r\n"
		  "Received: from [192.168.1.3] by 127.0.0.1 with ESMTPA Mon, 12 May 2025 00:59:00 +0000\r\n"
		  "Content-Type: text/plain; charset=\"UTF-8\"\r\n"
		  "Content-Transfer-Encoding: quoted-printable\r\n"
		  "X-Custom-Header: B valu\r\n"
		  "MIME-Version: 1.0\r\n"
		  "\r\n"
		  "Body text mostly here\r\n"
		  "in =33 msg3=32 =C5=A1=C3=A1=C5=A1a\r\n" }
	};
	guint ii;

	for (ii = 0; ii < G_N_ELEMENTS (data); ii++) {
		CamelMimeMessage *msg;
		GError *local_error = NULL;
		gboolean success;

		if (g_strcmp0 (uid, data[ii].uid) != 0)
			continue;

		msg = camel_mime_message_new ();

		success = camel_data_wrapper_construct_from_data_sync (CAMEL_DATA_WRAPPER (msg),
			data[ii].mime, strlen (data[ii].mime), NULL, &local_error);
		g_assert_no_error (local_error);
		g_assert_true (success);

		if (inout_nfo) {
			gchar *preview;

			camel_message_info_take_headers (inout_nfo, camel_medium_dup_headers (CAMEL_MEDIUM (msg)));

			preview = camel_mime_part_generate_preview (CAMEL_MIME_PART (msg), NULL, NULL);
			if (preview) {
				if (g_utf8_strlen (preview, -1) > 17) {
					gchar *cut = g_utf8_offset_to_pointer (preview, 17);
					*cut = '\0';
				}

				camel_message_info_set_preview (inout_nfo, preview);
				g_free (preview);
			}
		}

		if (out_message)
			*out_message = g_steal_pointer (&msg);

		g_clear_object (&msg);

		return;
	}

	g_assert_not_reached ();
}

#define TEST_TYPE_FOLDER (test_folder_get_type ())
G_DECLARE_FINAL_TYPE (TestFolder, test_folder, TEST, FOLDER, CamelFolder)

struct _TestFolder {
	CamelFolder parent_instance;

	guint32 n_called_get_message_info;
	guint32 n_called_get_message;
	guint32 n_called_search_header;
	guint32 n_called_search_body;
	gboolean message_info_with_headers;
	gboolean cache_message_info;
};

G_DEFINE_TYPE (TestFolder, test_folder, CAMEL_TYPE_FOLDER)

static gchar *
test_folder_get_filename (CamelFolder *folder,
			  const gchar *uid,
			  GError **error)
{
	return NULL;
}

static CamelMessageInfo *
test_folder_get_message_info (CamelFolder *folder,
			      const gchar *uid)
{
	TestFolder *self = TEST_FOLDER (folder);
	CamelFolderSummary *summary;
	CamelMessageInfo *nfo = NULL;

	summary = camel_folder_get_folder_summary (folder);
	if (summary)
		nfo = camel_folder_summary_peek_loaded (summary, uid);

	if (!nfo) {
		CamelStoreDB *sdb;
		gchar *bdata_ptr = NULL;
		GError *local_error = NULL;
		gboolean success;
		CamelStoreDBMessageRecord record = { 0, };

		sdb = camel_store_get_db (camel_folder_get_parent_store (folder));

		success = camel_store_db_read_message (sdb, camel_folder_get_full_name (folder), uid, &record, &local_error);
		g_assert_no_error (local_error);
		g_assert_true (success);

		nfo = camel_message_info_new (summary);

		g_assert_true (camel_message_info_load (nfo, &record, &bdata_ptr));

		camel_store_db_message_record_clear (&record);

		if (summary && self->cache_message_info)
			camel_folder_summary_add (summary, nfo, TRUE);
	}

	if (self->message_info_with_headers) {
		test_store_search_read_message_data (uid, nfo, NULL);
	}

	self->n_called_get_message_info++;

	return nfo;
}

static CamelMimeMessage *
test_folder_get_message_sync (CamelFolder *folder,
			      const gchar *message_uid,
			      GCancellable *cancellable,
			      GError **error)
{
	TestFolder *self = TEST_FOLDER (folder);
	CamelMimeMessage *msg = NULL;

	test_store_search_read_message_data (message_uid, NULL, &msg);

	g_assert_nonnull (msg);

	self->n_called_get_message++;

	return msg;
}

static gboolean
test_folder_synchronize_sync (CamelFolder *folder,
			      gboolean expunge,
			      GCancellable *cancellable,
			      GError **error)
{
	if (expunge) {
		CamelFolderSummary *summary = camel_folder_get_folder_summary (folder);
		CamelFolderChangeInfo *changes;
		GPtrArray *all_uids;
		guint ii;

		if (!camel_folder_summary_get_deleted_count (summary))
			return TRUE;

		all_uids = camel_folder_dup_uids (folder);
		if (!all_uids)
			return TRUE;

		changes = camel_folder_change_info_new ();

		for (ii = 0; ii < all_uids->len; ii++) {
			const gchar *uid = g_ptr_array_index (all_uids, ii);

			if ((camel_folder_summary_get_info_flags (summary, uid) & CAMEL_MESSAGE_DELETED) != 0) {
				camel_folder_change_info_remove_uid (changes, uid);
				camel_folder_summary_remove_uid (summary, uid);
			}
		}

		if (camel_folder_change_info_changed (changes))
			camel_folder_changed (folder, changes);

		camel_folder_change_info_free (changes);
		g_ptr_array_unref (all_uids);
	}

	return TRUE;
}

static gboolean
test_folder_search_header_sync (CamelFolder *folder,
				const gchar *header_name,
				/* const */ GPtrArray *words, /* gchar * */
				GPtrArray **out_uids, /* gchar * */
				GCancellable *cancellable,
				GError **error)
{
	TestFolder *self = TEST_FOLDER (folder);

	self->n_called_search_header++;

	return CAMEL_FOLDER_CLASS (test_folder_parent_class)->search_header_sync (folder, header_name, words, out_uids, cancellable, error);
}

static gboolean
test_folder_search_body_sync (CamelFolder *folder,
			      /* const */ GPtrArray *words, /* gchar * */
			      GPtrArray **out_uids, /* gchar * */
			      GCancellable *cancellable,
			      GError **error)
{
	TestFolder *self = TEST_FOLDER (folder);

	self->n_called_search_body++;

	return CAMEL_FOLDER_CLASS (test_folder_parent_class)->search_body_sync (folder, words, out_uids, cancellable, error);
}

static void
test_folder_class_init (TestFolderClass *klass)
{
	CamelFolderClass *folder_class;

	folder_class = CAMEL_FOLDER_CLASS (klass);
	folder_class->get_filename = test_folder_get_filename;
	folder_class->get_message_info = test_folder_get_message_info;
	folder_class->get_message_sync = test_folder_get_message_sync;
	folder_class->synchronize_sync = test_folder_synchronize_sync;
	folder_class->search_header_sync = test_folder_search_header_sync;
	folder_class->search_body_sync = test_folder_search_body_sync;
}

static void
test_folder_init (TestFolder *self)
{
	CamelFolder *folder = CAMEL_FOLDER (self);

	self->cache_message_info = TRUE;

	camel_folder_take_folder_summary (folder, camel_folder_summary_new (folder));
}

static CamelFolder *
test_folder_new (CamelStore *store,
		 const gchar *folder_name)
{
	const gchar *dash = strrchr (folder_name, '/');

	return g_object_new (TEST_TYPE_FOLDER,
		"parent-store", store,
		"display-name", dash ? dash + 1 : folder_name,
		"full-name", folder_name,
		NULL);
}

#define TEST_TYPE_STORE (test_store_get_type ())
G_DECLARE_FINAL_TYPE (TestStore, test_store, TEST, STORE, CamelStore)

struct _TestStore {
	CamelStore parent_instance;

	gchar *db_filename;
};

static GInitableIface *store_parent_initable_interface = NULL;

static void test_store_initable_init_iface (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (TestStore, test_store, CAMEL_TYPE_STORE,
	G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, test_store_initable_init_iface))

static CamelFolder *
test_store_get_folder_sync (CamelStore *store,
			    const gchar *folder_name,
			    CamelStoreGetFolderFlags flags,
			    GCancellable *cancellable,
			    GError **error)
{
	CamelStoreDB *sdb = camel_store_get_db (store);

	/* only known folders */
	g_assert_cmpint (camel_store_db_get_folder_id (sdb, folder_name), !=, 0);

	return test_folder_new (store, folder_name);
}

static gboolean
test_store_initable_init (GInitable *initable,
			  GCancellable *cancellable,
			  GError **error)
{
	CamelStore *store = CAMEL_STORE (initable);
	TestStore *self = TEST_STORE (initable);

	camel_store_set_flags (store, camel_store_get_flags (store) | CAMEL_STORE_USE_TEMP_DIR);

	if (!store_parent_initable_interface->init (initable, cancellable, error))
		return FALSE;

	self->db_filename = g_strdup (camel_db_get_filename (CAMEL_DB (camel_store_get_db (store))));

	return TRUE;
}

static void
test_store_initable_init_iface (GInitableIface *iface)
{
	store_parent_initable_interface = g_type_interface_peek_parent (iface);
	iface->init = test_store_initable_init;
}

static void
test_store_finalize (GObject *object)
{
	TestStore *self = TEST_STORE (object);

	g_assert_cmpint (g_unlink (self->db_filename), ==, 0);
	g_free (self->db_filename);

	G_OBJECT_CLASS (test_store_parent_class)->finalize (object);
}

static void
test_store_class_init (TestStoreClass *klass)
{
	CamelStoreClass *store_class;
	GObjectClass *object_class;

	store_class = CAMEL_STORE_CLASS (klass);
	store_class->get_folder_sync = test_store_get_folder_sync;

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = test_store_finalize;
}

static void
test_store_init (TestStore *self)
{
}

static CamelStore *
test_store_new_full (CamelSession *session,
		     const gchar *uid,
		     const gchar *display_name)
{
	static const CamelProvider provider = {
		.protocol = "none",
		.name = "test-none",
		.description = "Test provider",
		.domain = "mail",
		.flags = 0,
	};
	CamelStoreDBFolderRecord record = {
		.folder_name = NULL,
		.version = 4,
		.flags = 0,
		.nextuid = 0,
		.timestamp = 0,
		.saved_count = 0,
		.unread_count = 0,
		.deleted_count = 0,
		.junk_count = 0,
		.visible_count = 0,
		.jnd_count = 0,
		.bdata = NULL,
		.folder_id = 0
	};
	CamelStore *store;
	CamelStoreDB *sdb;
	GError *local_error = NULL;
	gboolean success;

	store = g_initable_new (TEST_TYPE_STORE, NULL, &local_error,
		"uid", uid,
		"display-name", display_name,
		"provider", &provider,
		"session", session,
		"with-proxy-resolver", FALSE,
		NULL);
	g_assert_no_error (local_error);
	g_assert_nonnull (store);

	sdb = camel_store_get_db (store);
	g_assert_nonnull (sdb);

	/* prepare the folders */
	success = camel_store_db_write_folder (sdb, "f1", &record, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);

	success = camel_store_db_write_folder (sdb, "f2", &record, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);

	success = camel_store_db_write_folder (sdb, "f3", &record, &local_error);
	g_assert_no_error (local_error);
	g_assert_true (success);

	return store;
}

static CamelStore *
test_store_new (void)
{
	CamelSession *session;
	CamelStore *store;

	session = test_session_new ();

	store = test_store_new_full (session, "test-store-search", "Test Store Search");

	g_object_set_data_full (G_OBJECT (store), "camel-session", session, g_object_unref);

	return store;
}

static void
test_add_messages (CamelFolder *folder,
		   ...) G_GNUC_NULL_TERMINATED;

static void
test_add_messages (CamelFolder *folder,
		   ...)
{
	CamelStore *store;
	CamelStoreDB *sdb;
	CamelStoreDBMessageRecord record = { 0, };
	va_list ap;
	guint32 folder_id;
	const gchar *folder_name;
	const gchar *tmp;
	gboolean any_set = FALSE;

	folder_name = camel_folder_get_full_name (folder);
	store = camel_folder_get_parent_store (folder);
	sdb = camel_store_get_db (store);
	folder_id = camel_store_db_get_folder_id (sdb, folder_name);
	g_assert_cmpint (folder_id, !=, 0);

	va_start (ap, folder);

	for (tmp = va_arg (ap, const gchar *); tmp; tmp = va_arg (ap, const gchar *)) {
		if (!*tmp) {
			if (any_set) {
				gboolean success;
				GError *local_error = NULL;

				g_assert_cmpstr (record.uid, !=, NULL);
				record.folder_id = 0;
				success = camel_store_db_write_message (sdb, folder_name, &record, &local_error);
				g_assert_no_error (local_error);
				g_assert_true (success);
			}

			memset (&record, 0, sizeof (record));
			any_set = FALSE;
			continue;
		}

		any_set = TRUE;

		if (g_str_equal (tmp, "uid")) {
			record.uid = va_arg (ap, const gchar *);
		} else if (g_str_equal (tmp, "subject")) {
			record.subject = va_arg (ap, const gchar *);
		} else if (g_str_equal (tmp, "from")) {
			record.from = va_arg (ap, const gchar *);
		} else if (g_str_equal (tmp, "to")) {
			record.to = va_arg (ap, const gchar *);
		} else if (g_str_equal (tmp, "cc")) {
			record.cc = va_arg (ap, const gchar *);
		} else if (g_str_equal (tmp, "mlist")) {
			record.mlist = va_arg (ap, const gchar *);
		} else if (g_str_equal (tmp, "labels")) {
			record.labels = va_arg (ap, gchar *);
		} else if (g_str_equal (tmp, "usertags")) {
			record.usertags = va_arg (ap, gchar *);
		} else if (g_str_equal (tmp, "flags")) {
			record.flags = va_arg (ap, guint32);
		} else if (g_str_equal (tmp, "dsent")) {
			record.dsent = va_arg (ap, gint64);
		} else if (g_str_equal (tmp, "dreceived")) {
			record.dreceived = va_arg (ap, gint64);
		} else if (g_str_equal (tmp, "size")) {
			record.size = va_arg (ap, guint32);
		} else if (g_str_equal (tmp, "part")) {
			record.part = va_arg (ap, gchar *);
		} else {
			g_error ("%s: Unknown field name '%s'", G_STRFUNC, tmp);
			g_assert_not_reached ();
		}
	}

	va_end (ap);

	if (any_set) {
		gboolean success;
		GError *local_error = NULL;

		g_assert_cmpstr (record.uid, !=, NULL);
		record.folder_id = 0;
		success = camel_store_db_write_message (sdb, folder_name, &record, &local_error);
		g_assert_no_error (local_error);
		g_assert_true (success);

		success = camel_folder_summary_load (camel_folder_get_folder_summary (folder), &local_error);
		g_assert_no_error (local_error);
		g_assert_true (success);
	}
}
