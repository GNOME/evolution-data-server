#include <string.h>

#include "camel-test.h"
#include "folders.h"
#include "messages.h"

/* check the total/unread is what we think it should be */
void
test_folder_counts (CamelFolder *folder,
                    gint total,
                    gint unread)
{
	GPtrArray *s;
	gint i, myunread;
	CamelMessageInfo *info;

	push ("test folder counts %d total %d unread", total, unread);

	/* use the summary */
	s = camel_folder_get_summary (folder);
	check (s != NULL);
	check (s->len == total);
	myunread = s->len;
	for (i = 0; i < s->len; i++) {
		info = s->pdata[i];
		if (camel_message_info_flags (info) & CAMEL_MESSAGE_SEEN)
			myunread--;
	}
	check (unread == myunread);
	camel_folder_free_summary (folder, s);

	/* use the uid list */
	s = camel_folder_get_uids (folder);
	check (s != NULL);
	check (s->len == total);
	myunread = s->len;
	for (i = 0; i < s->len; i++) {
		info = camel_folder_get_message_info (folder, s->pdata[i]);
		if (camel_message_info_flags (info) & CAMEL_MESSAGE_SEEN)
			myunread--;
		camel_message_info_unref (info);
	}
	check (unread == myunread);
	camel_folder_free_uids (folder, s);

	pull ();
}

static gint
safe_strcmp (const gchar *a,
             const gchar *b)
{
	if (a == NULL && b == NULL)
		return 0;
	if (a == NULL)
		return 1;
	if (b == NULL)
		return -1;
	return strcmp (a, b);
}

void
test_message_info (CamelMimeMessage *msg,
                   const CamelMessageInfo *info)
{
	check_msg (
		safe_strcmp (camel_message_info_subject (info), camel_mime_message_get_subject (msg)) == 0,
		"info->subject = '%s', get_subject () = '%s'", camel_message_info_subject (info), camel_mime_message_get_subject (msg));

	/* FIXME: testing from/cc/to, etc is more tricky */

	check (camel_message_info_date_sent (info) == camel_mime_message_get_date (msg, NULL));

	/* date received isn't set for messages that haven't been sent anywhere ... */
	/*check (info->date_received == camel_mime_message_get_date_received (msg, NULL));*/

	/* so is messageid/references, etc */
}

/* check a message is present */
void
test_folder_message (CamelFolder *folder,
                     const gchar *uid)
{
	CamelMimeMessage *msg;
	CamelMessageInfo *info;
	GPtrArray *s;
	gint i;
	gint found;
	GError *error = NULL;

	push ("uid %s is in folder", uid);

	/* first try getting info */
	info = camel_folder_get_message_info (folder, uid);
	check (info != NULL);
	check (strcmp (camel_message_info_uid (info), uid) == 0);
	camel_message_info_unref (info);

	/* then, getting message */
	msg = camel_folder_get_message_sync (folder, uid, NULL, &error);
	check_msg (error == NULL, "%s", error->message);
	check (msg != NULL);

	/* cross check with info */
	test_message_info (msg, info);

	g_object_unref (msg);

	/* see if it is in the summary (only once) */
	s = camel_folder_get_summary (folder);
	check (s != NULL);
	found = 0;
	for (i = 0; i < s->len; i++) {
		info = s->pdata[i];
		if (strcmp (camel_message_info_uid (info), uid) == 0)
			found++;
	}
	check (found == 1);
	camel_folder_free_summary (folder, s);

	/* check it is in the uid list */
	s = camel_folder_get_uids (folder);
	check (s != NULL);
	found = 0;
	for (i = 0; i < s->len; i++) {
		if (strcmp (s->pdata[i], uid) == 0)
			found++;
	}
	check (found == 1);
	camel_folder_free_uids (folder, s);

	g_clear_error (&error);

	pull ();
}

/* check message not present */
void
test_folder_not_message (CamelFolder *folder,
                         const gchar *uid)
{
	CamelMimeMessage *msg;
	CamelMessageInfo *info;
	GPtrArray *s;
	gint i;
	gint found;
	GError *error = NULL;

	push ("uid '%s' is not in folder", uid);

	/* first try getting info */
	push ("no message info");
	info = camel_folder_get_message_info (folder, uid);
	check (info == NULL);
	pull ();

	/* then, getting message */
	push ("no message");
	msg = camel_folder_get_message_sync (folder, uid, NULL, &error);
	check (error != NULL);
	check (msg == NULL);
	g_clear_error (&error);
	pull ();

	/* see if it is not in the summary (only once) */
	push ("not in summary list");
	s = camel_folder_get_summary (folder);
	check (s != NULL);
	found = 0;
	for (i = 0; i < s->len; i++) {
		info = s->pdata[i];
		if (strcmp (camel_message_info_uid (info), uid) == 0)
			found++;
	}
	check (found == 0);
	camel_folder_free_summary (folder, s);
	pull ();

	/* check it is not in the uid list */
	push ("not in uid list");
	s = camel_folder_get_uids (folder);
	check (s != NULL);
	found = 0;
	for (i = 0; i < s->len; i++) {
		if (strcmp (s->pdata[i], uid) == 0)
			found++;
	}
	check (found == 0);
	camel_folder_free_uids (folder, s);
	pull ();

	g_clear_error (&error);

	pull ();
}

/* test basic store operations on folders */
/* TODO: Add subscription stuff */
void
test_folder_basic (CamelSession *session,
                   const gchar *storename,
                   gint local,
                   gint spool)
{
	CamelStore *store;
	CamelFolder *folder;
	CamelService *service;
	gchar *what = g_strdup_printf ("testing store: %s", storename);
	GError *error = NULL;

	camel_test_start (what);
	test_free (what);

	push ("getting store");
	service = camel_session_add_service (
		session, storename, storename, CAMEL_PROVIDER_STORE, &error);
	check_msg (error == NULL, "adding store: %s", error->message);
	check (CAMEL_IS_STORE (service));
	store = CAMEL_STORE (service);
	pull ();

	/* local providers == no inbox */
	push ("getting inbox folder");
	folder = camel_store_get_inbox_folder_sync (store, NULL, &error);
	if (local) {
		/* Well, maildir can have an inbox */
		if (folder) {
			check (error == NULL);
			check_unref (folder, 1);
		} else {
			check (error != NULL);
		}
	} else {
		check_msg (error == NULL, "%s", error->message);
		check (folder != NULL);
		check_unref (folder, 2);
	}
	g_clear_error (&error);
	pull ();

	push ("getting a non-existant folder, no create");
	folder = camel_store_get_folder_sync (
		store, "unknown", 0, NULL, &error);
	check (error != NULL);
	check (folder == NULL);
	g_clear_error (&error);
	pull ();

	if (!spool) {
		push ("getting a non-existant folder, with create");
		folder = camel_store_get_folder_sync (
			store, "testbox", CAMEL_STORE_FOLDER_CREATE,
			NULL, &error);
		check_msg (error == NULL, "%s", error->message);
		check (folder != NULL);
		if (local)
			check_unref (folder, 1);
		else
			check_unref (folder, 2);
		g_clear_error (&error);
		pull ();

		push ("getting an existing folder");
		folder = camel_store_get_folder_sync (
			store, "testbox", 0, NULL, &error);
		check_msg (error == NULL, "%s", error->message);
		check (folder != NULL);
		if (local)
			check_unref (folder, 1);
		else
			check_unref (folder, 2);
		g_clear_error (&error);
		pull ();

		push ("renaming a non-existant folder");
		camel_store_rename_folder_sync (
			store, "unknown1", "unknown2", NULL, &error);
		check (error != NULL);
		g_clear_error (&error);
		pull ();

		push ("renaming an existing folder");
		camel_store_rename_folder_sync (
			store, "testbox", "testbox2", NULL, &error);
		check_msg (error == NULL, "%s", error->message);
		g_clear_error (&error);
		pull ();

		push ("opening the old name of a renamed folder");
		folder = camel_store_get_folder_sync (
			store, "testbox", 0, NULL, &error);
		check (error != NULL);
		check (folder == NULL);
		g_clear_error (&error);
		pull ();

		push ("opening the new name of a renamed folder");
		folder = camel_store_get_folder_sync (
			store, "testbox2", 0, NULL, &error);
		check_msg (error == NULL, "%s", error->message);
		check (folder != NULL);
		if (local)
			check_unref (folder, 1);
		else
			check_unref (folder, 2);
		pull ();
	}

	push ("deleting a non-existant folder");
	camel_store_delete_folder_sync (store, "unknown", NULL, &error);
	check (error != NULL);
	g_clear_error (&error);
	pull ();

	if (!spool) {
		push ("deleting an existing folder");
		camel_store_delete_folder_sync (
			store, "testbox2", NULL, &error);
		check_msg (error == NULL, "%s", error->message);
		g_clear_error (&error);
		pull ();
	}

	push ("opening a folder that has been deleted");
	folder = camel_store_get_folder_sync (
		store, "testbox2", 0, NULL, &error);
	check (error != NULL);
	check (folder == NULL);
	g_clear_error (&error);
	pull ();

	check_unref (store, 1);

	camel_test_end ();
}

/* todo: cross-check everything with folder_info checks as well */
/* this should probably take a folder instead of a session ... */
void
test_folder_message_ops (CamelSession *session,
                         const gchar *name,
                         gint local,
                         const gchar *mailbox)
{
	CamelStore *store;
	CamelService *service;
	CamelFolder *folder;
	CamelMimeMessage *msg;
	gint j;
	gint indexed, max;
	GPtrArray *uids;
	CamelMessageInfo *info;
	GError *error = NULL;

	max = local ? 2 : 1;

	for (indexed = 0; indexed < max; indexed++) {
		gchar *what = g_strdup_printf ("folder ops: %s %s", name, local ? (indexed?"indexed":"non-indexed"):"");
		gint flags;

		camel_test_start (what);
		test_free (what);

		push ("getting store");
		service = camel_session_add_service (
			session, name, name, CAMEL_PROVIDER_STORE, &error);
		check_msg (error == NULL, "adding store: %s", error->message);
		check (CAMEL_IS_STORE (service));
		store = CAMEL_STORE (service);
		g_clear_error (&error);
		pull ();

		push ("creating %sindexed folder", indexed?"":"non-");
		if (indexed)
			flags = CAMEL_STORE_FOLDER_CREATE | CAMEL_STORE_FOLDER_BODY_INDEX;
		else
			flags = CAMEL_STORE_FOLDER_CREATE;
		folder = camel_store_get_folder_sync (
			store, mailbox, flags, NULL, &error);

		/* we can't create mailbox outside of namespace, since we have no api for it, try
		 * using inbox namespace, works for courier */
		if (folder == NULL) {
			gchar *mbox = g_strdup_printf ("INBOX/%s", mailbox);
			mailbox = mbox;
			g_clear_error (&error);
			folder = camel_store_get_folder_sync (
				store, mailbox, flags, NULL, &error);
		}

		check_msg (error == NULL, "%s", error->message);
		check (folder != NULL);

		/* verify empty/can't get nonexistant stuff */
		test_folder_counts (folder, 0, 0);
		test_folder_not_message (folder, "0");
		test_folder_not_message (folder, "");

		for (j = 0; j < 10; j++) {
			gchar *content, *subject;

			push ("creating test message");
			msg = test_message_create_simple ();
			content = g_strdup_printf ("Test message %d contents\n\n", j);
			test_message_set_content_simple (
				(CamelMimePart *) msg, 0, "text/plain",
							content, strlen (content));
			test_free (content);
			subject = g_strdup_printf ("Test message %d", j);
			camel_mime_message_set_subject (msg, subject);
			pull ();

			push ("appending simple message %d", j);
			camel_folder_append_message_sync (
				folder, msg, NULL, NULL, NULL, &error);
			check_msg (error == NULL, "%s", error->message);

#if 0
			/* sigh, this shouldn't be required, but the imap code is too dumb to do it itself */
			if (!local) {
				push ("forcing a refresh of folder updates");
				camel_folder_refresh_info (folder, ex);
				check_msg (error == NULL, "%s", error->message);
				pull ();
			}
#endif
			/*if (!local)
			  camel_test_nonfatal ("unread counts dont seem right for imap");*/

			test_folder_counts (folder, j + 1, j + 1);

			/*if (!local)
			  camel_test_fatal ();*/

			push ("checking it is in the right uid slot & exists");
			uids = camel_folder_get_uids (folder);
			check (uids != NULL);
			check (uids->len == j + 1);
			if (uids->len > j)
				test_folder_message (folder, uids->pdata[j]);
			pull ();

			push ("checking it is the right message (subject): %s", subject);
			if (uids->len > j) {
				info = camel_folder_get_message_info (folder, uids->pdata[j]);
				check (info != NULL);
				check_msg (
					strcmp (camel_message_info_subject (info), subject) == 0,
					"info->subject %s", camel_message_info_subject (info));
				camel_message_info_unref (info);
			}
			camel_folder_free_uids (folder, uids);
			pull ();

			test_free (subject);

			/*if (!local)
			  camel_test_fatal ();*/

			check_unref (msg, 1);
			pull ();
		}

		if (local)
			check_unref (folder, 1);
		else
			check_unref (folder, 2);
		pull ();

#if 0
		push ("deleting test folder, with messages in it");
		camel_store_delete_folder (store, mailbox, ex);
		check (camel_exception_is_set (ex));
		camel_exception_clear (ex);
		pull ();
#endif

		push ("re-opening folder");
		folder = camel_store_get_folder_sync (
			store, mailbox, flags, NULL, &error);
		check_msg (error == NULL, "%s", error->message);
		check (folder != NULL);
		g_clear_error (&error);

			/* verify counts */
		test_folder_counts (folder, 10, 10);

		/* re-check uid's, after a reload */
		uids = camel_folder_get_uids (folder);
		check (uids != NULL);
		check (uids->len == 10);
		for (j = 0; j < 10; j++) {
			gchar *subject = g_strdup_printf ("Test message %d", j);

			push ("verify reload of %s", subject);
			test_folder_message (folder, uids->pdata[j]);

			info = camel_folder_get_message_info (folder, uids->pdata[j]);
			check_msg (
				strcmp (camel_message_info_subject (info), subject) == 0,
				"info->subject %s", camel_message_info_subject (info));
			test_free (subject);
			camel_message_info_unref (info);
			pull ();
		}

		push ("deleting first message & expunging");
		camel_folder_delete_message (folder, uids->pdata[0]);
		test_folder_counts (folder, 10, 9);
		camel_folder_expunge_sync (folder, NULL, &error);
		check_msg (error == NULL, "%s", error->message);
		g_clear_error (&error);
		test_folder_not_message (folder, uids->pdata[0]);
		test_folder_counts (folder, 9, 9);

		camel_folder_free_uids (folder, uids);

		uids = camel_folder_get_uids (folder);
		check (uids != NULL);
		check (uids->len == 9);
		for (j = 0; j < 9; j++) {
			gchar *subject = g_strdup_printf ("Test message %d", j + 1);

			push ("verify after expunge of %s", subject);
			test_folder_message (folder, uids->pdata[j]);

			info = camel_folder_get_message_info (folder, uids->pdata[j]);
			check_msg (
				strcmp (camel_message_info_subject (info), subject) == 0,
				"info->subject %s", camel_message_info_subject (info));
			test_free (subject);
			camel_message_info_unref (info);
			pull ();
		}
		pull ();

		push ("deleting last message & expunging");
		camel_folder_delete_message (folder, uids->pdata[8]);
		/* sync? */
		test_folder_counts (folder, 9, 8);
		camel_folder_expunge_sync (folder, NULL, &error);
		check_msg (error == NULL, "%s", error->message);
		g_clear_error (&error);
		test_folder_not_message (folder, uids->pdata[8]);
		test_folder_counts (folder, 8, 8);

		camel_folder_free_uids (folder, uids);

		uids = camel_folder_get_uids (folder);
		check (uids != NULL);
		check (uids->len == 8);
		for (j = 0; j < 8; j++) {
			gchar *subject = g_strdup_printf ("Test message %d", j + 1);

			push ("verify after expunge of %s", subject);
			test_folder_message (folder, uids->pdata[j]);

			info = camel_folder_get_message_info (folder, uids->pdata[j]);
			check_msg (
				strcmp (camel_message_info_subject (info), subject) == 0,
				"info->subject %s", camel_message_info_subject (info));
			test_free (subject);
			camel_message_info_unref (info);
			pull ();
		}
		pull ();

		push ("deleting all messages & expunging");
		for (j = 0; j < 8; j++) {
			camel_folder_delete_message (folder, uids->pdata[j]);
		}
		/* sync? */
		test_folder_counts (folder, 8, 0);
		camel_folder_expunge_sync (folder, NULL, &error);
		check_msg (error == NULL, "%s", error->message);
		g_clear_error (&error);
		for (j = 0; j < 8; j++) {
			test_folder_not_message (folder, uids->pdata[j]);
		}
		test_folder_counts (folder, 0, 0);

		camel_folder_free_uids (folder, uids);
		pull ();

		if (local)
			check_unref (folder, 1);
		else
			check_unref (folder, 2);
		pull (); /* re-opening folder */

		if (g_ascii_strcasecmp (mailbox, "INBOX") != 0) {
			push ("deleting test folder, with no messages in it");
			camel_store_delete_folder_sync (
				store, mailbox, NULL, &error);
			check_msg (error == NULL, "%s", error->message);
			g_clear_error (&error);
			pull ();
		}

		if (!local) {
			push ("disconneect service");
			camel_service_disconnect_sync (
				CAMEL_SERVICE (store), TRUE, NULL, &error);
			check_msg (error == NULL, "%s", error->message);
			g_clear_error (&error);
			pull ();
		}

		check_unref (store, 1);
		camel_test_end ();
	}
}
