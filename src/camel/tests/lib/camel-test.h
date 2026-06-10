/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef CAMEL_TEST_H
#define CAMEL_TEST_H

#include "evolution-data-server-config.h"

#include <stdlib.h>
#include <glib/gstdio.h>
#include <camel/camel.h>

void camel_test_init (gint *argc, gchar ***argv);
void camel_test_shutdown (void);

const gchar *camel_test_get_dir (void);
gchar *camel_test_get_data_file (const gchar *filename);

#define DELAY_TIMEOUT_SECONDS 5

/* utility functions */
/* compare strings, ignore whitespace though */
gint string_equal (const gchar *a, const gchar *b);

gboolean	test_util_abort_on_timeout_cb	(gpointer user_data) G_GNUC_NORETURN;

#define TEST_TYPE_SESSION (test_session_get_type ())
G_DECLARE_FINAL_TYPE (TestSession, test_session, TEST, SESSION, CamelSession)

struct _TestSession {
	CamelSession parent_instance;

	guint32 n_called_addressbook_contains;
	guint32 n_pending_jobs;
};

CamelSession *	test_session_new		(void);
void		test_session_check_finalized	(void);
void		test_session_wait_for_pending_jobs
						(void);

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

CamelFolder *	test_folder_new			(CamelStore *store,
						 const gchar *folder_name);

#define TEST_TYPE_STORE (test_store_get_type ())
G_DECLARE_FINAL_TYPE (TestStore, test_store, TEST, STORE, CamelStore)

struct _TestStore {
	CamelStore parent_instance;

	gchar *db_filename;
};

CamelStore *	test_store_new			(void);
CamelStore *	test_store_new_full		(CamelSession *session,
						 const gchar *uid,
						 const gchar *display_name);

void		test_add_messages		(CamelFolder *folder,
						 ...) G_GNUC_NULL_TERMINATED;

void		test_store_search_read_message_data
						(const gchar *uid,
						 CamelMessageInfo *inout_nfo,
						 CamelMimeMessage **out_message);

#endif /* CAMEL_TEST_H */
