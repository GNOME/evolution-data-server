/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-book-backend-file.c - File contact backend.
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Authors: Nat Friedman <nat@novell.com>
 *          Chris Toshok <toshok@ximian.com>
 *          Hans Petter Jansson <hpj@novell.com>
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include "db.h"
#include <sys/stat.h>
#include <sys/time.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>

#include "libebackend/e-dbhash.h"
#include "libebackend/e-db3-utils.h"

#include "libedataserver/e-data-server-util.h"
#include "libedataserver/e-flag.h"

#include "libebook/e-contact.h"

#include "libedata-book/e-book-backend-sexp.h"
#include "libedata-book/e-book-backend-summary.h"
#include "libedata-book/e-data-book.h"
#include "libedata-book/e-data-book-view.h"

#include "e-book-backend-file.h"

#define d(x)

#define CHANGES_DB_SUFFIX ".changes.db"

#define E_BOOK_BACKEND_FILE_VERSION_NAME "PAS-DB-VERSION"
#define E_BOOK_BACKEND_FILE_VERSION "0.2"

#define PAS_ID_PREFIX "pas-id-"
#define SUMMARY_FLUSH_TIMEOUT 5000

G_DEFINE_TYPE (EBookBackendFile, e_book_backend_file, E_TYPE_BOOK_BACKEND_SYNC)

struct _EBookBackendFilePrivate {
	gchar     *dirname;
	gchar     *filename;
	gchar     *summary_filename;
	DB       *file_db;
	DB_ENV   *env;
	EBookBackendSummary *summary;
	/* for future use */
	gpointer reserved1;
	gpointer reserved2;
	gpointer reserved3;
	gpointer reserved4;
};

G_LOCK_DEFINE_STATIC (global_env);
static struct {
	gint ref_count;
	DB_ENV *env;
} global_env;

static EBookBackendSyncStatus
db_error_to_status (const gint db_error)
{
	switch (db_error) {
	case 0:
		return GNOME_Evolution_Addressbook_Success;
	case DB_NOTFOUND:
		return GNOME_Evolution_Addressbook_ContactNotFound;
	case EACCES:
		return GNOME_Evolution_Addressbook_PermissionDenied;
	default:
		return GNOME_Evolution_Addressbook_OtherError;
	}
}

static void
string_to_dbt(const gchar *str, DBT *dbt)
{
	memset (dbt, 0, sizeof (*dbt));
	dbt->data = (gpointer)str;
	dbt->size = strlen (str) + 1;
	dbt->flags = DB_DBT_USERMEM;
}

static EContact*
create_contact (gchar *uid, const gchar *vcard)
{
	EContact *contact = e_contact_new_from_vcard (vcard);
	if (!e_contact_get_const (contact, E_CONTACT_UID))
		e_contact_set (contact, E_CONTACT_UID, uid);

	return contact;
}

static gboolean
build_summary (EBookBackendFilePrivate *bfpriv)
{
	DB             *db = bfpriv->file_db;
	DBC            *dbc;
	gint            db_error;
	DBT  id_dbt, vcard_dbt;

	db_error = db->cursor (db, NULL, &dbc, 0);

	if (db_error != 0) {
		g_warning (G_STRLOC ": db->cursor failed with %s", db_strerror (db_error));
		return FALSE;
	}

	memset (&vcard_dbt, 0, sizeof (vcard_dbt));
	memset (&id_dbt, 0, sizeof (id_dbt));
	db_error = dbc->c_get(dbc, &id_dbt, &vcard_dbt, DB_FIRST);

	while (db_error == 0) {

		/* don't include the version in the list of cards */
		if (id_dbt.size != strlen(E_BOOK_BACKEND_FILE_VERSION_NAME) + 1
		    || strcmp (id_dbt.data, E_BOOK_BACKEND_FILE_VERSION_NAME)) {
			EContact *contact = create_contact (id_dbt.data, vcard_dbt.data);
			e_book_backend_summary_add_contact (bfpriv->summary, contact);
			g_object_unref (contact);
		}

		db_error = dbc->c_get(dbc, &id_dbt, &vcard_dbt, DB_NEXT);

	}

	dbc->c_close (dbc);

	return TRUE;
}

static gchar *
e_book_backend_file_create_unique_id (void)
{
	/* use a 32 counter and the 32 bit timestamp to make an id.
	   it's doubtful 2^32 id's will be created in a second, so we
	   should be okay. */
	static guint c = 0;
	return g_strdup_printf (PAS_ID_PREFIX "%08lX%08X", time(NULL), c++);
}

static void
set_revision (EContact *contact)
{
	gchar time_string[100] = {0};
	const struct tm *tm = NULL;
	time_t t;

	t = time(NULL);
	tm = gmtime (&t);
	if (tm)
		strftime (time_string, 100, "%Y-%m-%dT%H:%M:%SZ", tm);
	e_contact_set (contact, E_CONTACT_REV, time_string);

}

static EBookBackendSyncStatus
do_create(EBookBackendFile  *bf,
	  const gchar      *vcard_req,
	  EContact **contact)
{
	DB             *db = bf->priv->file_db;
	DBT            id_dbt, vcard_dbt;
	gint            db_error;
	gchar           *id;
	gchar           *vcard;
	const gchar *rev;

	g_assert (bf);
	g_assert (vcard_req);
	g_assert (contact);

	id = e_book_backend_file_create_unique_id ();

	string_to_dbt (id, &id_dbt);

	*contact = e_contact_new_from_vcard (vcard_req);
	e_contact_set (*contact, E_CONTACT_UID, id);
	rev = e_contact_get_const (*contact,  E_CONTACT_REV);
	if (!(rev && *rev))
		set_revision (*contact);

	vcard = e_vcard_to_string (E_VCARD (*contact), EVC_FORMAT_VCARD_30);

	string_to_dbt (vcard, &vcard_dbt);

	db_error = db->put (db, NULL, &id_dbt, &vcard_dbt, 0);

	g_free (vcard);

	if (0 == db_error) {
		db_error = db->sync (db, 0);
		if (db_error != 0) {
			g_warning ("db->sync failed with %s", db_strerror (db_error));
		}
	} else {
		g_warning (G_STRLOC ": db->put failed with %s", db_strerror (db_error));
		g_object_unref (*contact);
		*contact = NULL;
	}

	g_free (id);
	return db_error_to_status (db_error);
}

static EBookBackendSyncStatus
e_book_backend_file_create_contact (EBookBackendSync *backend,
				    EDataBook *book,
				    guint32 opid,
				    const gchar *vcard,
				    EContact **contact)
{
	EBookBackendSyncStatus status;
	EBookBackendFile *bf = E_BOOK_BACKEND_FILE (backend);

	status = do_create (bf, vcard, contact);
	if (status == GNOME_Evolution_Addressbook_Success) {
		e_book_backend_summary_add_contact (bf->priv->summary, *contact);
	}
	return status;
}

static EBookBackendSyncStatus
e_book_backend_file_remove_contacts (EBookBackendSync *backend,
				     EDataBook *book,
				     guint32 opid,
				     GList *id_list,
				     GList **ids)
{
	EBookBackendFile *bf = E_BOOK_BACKEND_FILE (backend);
	DB             *db = bf->priv->file_db;
	DBT            id_dbt;
	gint            db_error;
	gchar          *id;
	GList         *l;
	GList         *removed_cards = NULL;
	GNOME_Evolution_Addressbook_CallStatus rv = GNOME_Evolution_Addressbook_Success;

	for (l = id_list; l; l = l->next) {
		id = l->data;

		string_to_dbt (id, &id_dbt);

		db_error = db->del (db, NULL, &id_dbt, 0);
		if (0 != db_error) {
			g_warning (G_STRLOC ": db->del failed with %s", db_strerror (db_error));
			rv = db_error_to_status (db_error);
			continue;
		}

		removed_cards = g_list_prepend (removed_cards, id);
	}

	/* if we actually removed some, try to sync */
	if (removed_cards) {
		db_error = db->sync (db, 0);
		if (db_error != 0)
			g_warning (G_STRLOC ": db->sync failed with %s", db_strerror (db_error));
	}

	*ids = removed_cards;

	for (l = removed_cards; l; l = l->next) {
		gchar *id = l->data;
		e_book_backend_summary_remove_contact (bf->priv->summary, id);
	}

	return rv;
}

static EBookBackendSyncStatus
e_book_backend_file_modify_contact (EBookBackendSync *backend,
				    EDataBook *book,
				    guint32 opid,
				    const gchar *vcard,
				    EContact **contact)
{
	EBookBackendFile *bf = E_BOOK_BACKEND_FILE (backend);
	DB             *db = bf->priv->file_db;
	DBT            id_dbt, vcard_dbt;
	gint            db_error;
	const gchar    *id, *lookup_id;
	gchar          *vcard_with_rev;

	*contact = e_contact_new_from_vcard (vcard);
	id = e_contact_get_const (*contact, E_CONTACT_UID);

	if (id == NULL)
		return GNOME_Evolution_Addressbook_OtherError;

	/* update the revision (modified time of contact) */
	set_revision (*contact);
	vcard_with_rev = e_vcard_to_string (E_VCARD (*contact), EVC_FORMAT_VCARD_30);

	/* This is disgusting, but for a time cards were added with
           ID's that are no longer used (they contained both the uri
           and the id.) If we recognize it as a uri (file:///...) trim
           off everything before the last '/', and use that as the
           id.*/
	if (!strncmp (id, "file:///", strlen ("file:///"))) {
		lookup_id = strrchr (id, '/') + 1;
	}
	else
		lookup_id = id;

	string_to_dbt (lookup_id, &id_dbt);
	string_to_dbt (vcard_with_rev, &vcard_dbt);

	db_error = db->put (db, NULL, &id_dbt, &vcard_dbt, 0);

	if (0 == db_error) {
		db_error = db->sync (db, 0);
		if (db_error != 0) {
			g_warning (G_STRLOC ": db->sync failed with %s", db_strerror (db_error));
		} else {
			e_book_backend_summary_remove_contact (bf->priv->summary, id);
			e_book_backend_summary_add_contact (bf->priv->summary, *contact);
		}
	} else {
		g_warning (G_STRLOC ": db->put failed with %s", db_strerror(db_error));
	}
	g_free (vcard_with_rev);

	if (0 == db_error)
		return GNOME_Evolution_Addressbook_Success;
	else
		return db_error_to_status (db_error);
}

static EBookBackendSyncStatus
e_book_backend_file_get_contact (EBookBackendSync *backend,
				 EDataBook *book,
				 guint32 opid,
				 const gchar *id,
				 gchar **vcard)
{
	EBookBackendFile *bf;
	DB             *db;
	DBT             id_dbt, vcard_dbt;
	gint             db_error = 0;

	bf = E_BOOK_BACKEND_FILE (backend);
	db = bf->priv->file_db;

	string_to_dbt (id, &id_dbt);
	memset (&vcard_dbt, 0, sizeof (vcard_dbt));
	vcard_dbt.flags = DB_DBT_MALLOC;

	db_error = db->get (db, NULL, &id_dbt, &vcard_dbt, 0);

	if (db_error == 0) {
		*vcard = vcard_dbt.data;
		return GNOME_Evolution_Addressbook_Success;
	} else {
		g_warning (G_STRLOC ": db->get failed with %s", db_strerror (db_error));
		*vcard = g_strdup ("");
		return GNOME_Evolution_Addressbook_ContactNotFound;
	}
}

static EBookBackendSyncStatus
e_book_backend_file_get_contact_list (EBookBackendSync *backend,
				      EDataBook *book,
				      guint32 opid,
				      const gchar *query,
				      GList **contacts)
{
	EBookBackendFile *bf = E_BOOK_BACKEND_FILE (backend);
	DB             *db = bf->priv->file_db;
	DBC            *dbc;
	gint            db_error;
	DBT  id_dbt, vcard_dbt;
	EBookBackendSExp *card_sexp = NULL;
	gboolean search_needed;
	const gchar *search = query;
	GList *contact_list = NULL;
	EBookBackendSyncStatus status;

	d(printf ("e_book_backend_file_get_contact_list (%s)\n", search));
	status = GNOME_Evolution_Addressbook_Success;
	if (e_book_backend_summary_is_summary_query (bf->priv->summary, search)) {

		/* do a summary query */
		GPtrArray *ids = e_book_backend_summary_search (bf->priv->summary, search);
		gint i;

		if (!ids)
			return GNOME_Evolution_Addressbook_ContactNotFound;

		for (i = 0; i < ids->len; i ++) {
			gchar *id = g_ptr_array_index (ids, i);
			string_to_dbt (id, &id_dbt);
			memset (&vcard_dbt, 0, sizeof (vcard_dbt));
			vcard_dbt.flags = DB_DBT_MALLOC;

			db_error = db->get (db, NULL, &id_dbt, &vcard_dbt, 0);
			if (db_error == 0) {
				contact_list = g_list_prepend (contact_list, vcard_dbt.data);
			} else {
				g_warning (G_STRLOC ": db->get failed with %s", db_strerror (db_error));
				status = db_error_to_status (db_error);
				break;
			}
		}
		g_ptr_array_free (ids, TRUE);
	} else {
		search_needed = TRUE;
		if (!strcmp (search, "(contains \"x-evolution-any-field\" \"\")"))
			search_needed = FALSE;

		card_sexp = e_book_backend_sexp_new (search);
		if (!card_sexp) {
			/* XXX this needs to be an invalid query error of some sort*/
			return GNOME_Evolution_Addressbook_OtherError;
		}

		db_error = db->cursor (db, NULL, &dbc, 0);

		if (db_error != 0) {
			g_warning (G_STRLOC ": db->cursor failed with %s", db_strerror (db_error));
			/* XXX this needs to be some CouldNotOpen error */
			return db_error_to_status (db_error);
		}

		memset (&vcard_dbt, 0, sizeof (vcard_dbt));
		vcard_dbt.flags = DB_DBT_MALLOC;
		memset (&id_dbt, 0, sizeof (id_dbt));
		db_error = dbc->c_get(dbc, &id_dbt, &vcard_dbt, DB_FIRST);

		while (db_error == 0) {

			/* don't include the version in the list of cards */
			if (id_dbt.size != strlen(E_BOOK_BACKEND_FILE_VERSION_NAME) + 1
			    || strcmp (id_dbt.data, E_BOOK_BACKEND_FILE_VERSION_NAME)) {

				if ((!search_needed) || (card_sexp != NULL && e_book_backend_sexp_match_vcard  (card_sexp, vcard_dbt.data))) {
					contact_list = g_list_prepend (contact_list, vcard_dbt.data);
				}
			}

			db_error = dbc->c_get(dbc, &id_dbt, &vcard_dbt, DB_NEXT);

		}
		g_object_unref (card_sexp);

		if (db_error == DB_NOTFOUND) {
			status = GNOME_Evolution_Addressbook_Success;
		} else {
			g_warning (G_STRLOC ": dbc->c_get failed with %s", db_strerror (db_error));
			status = db_error_to_status (db_error);
		}

		db_error = dbc->c_close(dbc);
		if (db_error != 0) {
			g_warning (G_STRLOC ": dbc->c_close failed with %s", db_strerror (db_error));
		}
	}

	*contacts = contact_list;
	return status;
}

typedef struct {
	EBookBackendFile *bf;
	GThread *thread;
	EFlag *running;
} FileBackendSearchClosure;

static void
closure_destroy (FileBackendSearchClosure *closure)
{
	d(printf ("destroying search closure\n"));
	e_flag_free (closure->running);
	g_free (closure);
}

static FileBackendSearchClosure*
init_closure (EDataBookView *book_view, EBookBackendFile *bf)
{
	FileBackendSearchClosure *closure = g_new (FileBackendSearchClosure, 1);

	closure->bf = bf;
	closure->thread = NULL;
	closure->running = e_flag_new ();

	g_object_set_data_full (G_OBJECT (book_view), "EBookBackendFile.BookView::closure",
				closure, (GDestroyNotify)closure_destroy);

	return closure;
}

static FileBackendSearchClosure*
get_closure (EDataBookView *book_view)
{
	return g_object_get_data (G_OBJECT (book_view), "EBookBackendFile.BookView::closure");
}

static gpointer
book_view_thread (gpointer data)
{
	EDataBookView *book_view;
	FileBackendSearchClosure *closure;
	EBookBackendFile *bf;
	const gchar *query;
	DB  *db;
	DBT id_dbt, vcard_dbt;
	gint db_error;
	gboolean allcontacts;

	g_return_val_if_fail (E_IS_DATA_BOOK_VIEW (data), NULL);

	book_view = data;
	closure = get_closure (book_view);
	if (!closure) {
		g_warning (G_STRLOC ": NULL closure in book view thread");
		return NULL;
	}
	bf = closure->bf;

	d(printf ("starting initial population of book view\n"));

	/* ref the book view because it'll be removed and unrefed
	   when/if it's stopped */
	e_data_book_view_ref (book_view);

	db = bf->priv->file_db;
	query = e_data_book_view_get_card_query (book_view);

	if ( ! strcmp (query, "(contains \"x-evolution-any-field\" \"\")")) {
		e_data_book_view_notify_status_message (book_view, _("Loading..."));
		allcontacts = TRUE;
	} else {
		e_data_book_view_notify_status_message (book_view, _("Searching..."));
		allcontacts = FALSE;
	}

	d(printf ("signalling parent thread\n"));
	e_flag_set (closure->running);

	if (e_book_backend_summary_is_summary_query (bf->priv->summary, query)) {
		/* do a summary query */
		GPtrArray *ids = e_book_backend_summary_search (bf->priv->summary, e_data_book_view_get_card_query (book_view));
		gint i;

		if (!ids)
			goto done;

		for (i = 0; i < ids->len; i ++) {
			gchar *id = g_ptr_array_index (ids, i);

			if (!e_flag_is_set (closure->running))
				break;

			string_to_dbt (id, &id_dbt);
			memset (&vcard_dbt, 0, sizeof (vcard_dbt));
			vcard_dbt.flags = DB_DBT_MALLOC;

			db_error = db->get (db, NULL, &id_dbt, &vcard_dbt, 0);

			if (db_error == 0) {
				e_data_book_view_notify_update_prefiltered_vcard (book_view, id, vcard_dbt.data);
			}
			else {
				g_warning (G_STRLOC ": db->get failed with %s", db_strerror (db_error));
			}
		}

		g_ptr_array_free (ids, TRUE);
	}
	else {
		/* iterate over the db and do the query there */
		DBC    *dbc;

		memset (&id_dbt, 0, sizeof (id_dbt));
		memset (&vcard_dbt, 0, sizeof (vcard_dbt));
		vcard_dbt.flags = DB_DBT_MALLOC;

		db_error = db->cursor (db, NULL, &dbc, 0);
		if (db_error == 0) {

			db_error = dbc->c_get(dbc, &id_dbt, &vcard_dbt, DB_FIRST);
			while (db_error == 0) {

				if (!e_flag_is_set (closure->running))
					break;

				/* don't include the version in the list of cards */
				if (strcmp (id_dbt.data, E_BOOK_BACKEND_FILE_VERSION_NAME)) {
					if (allcontacts)
						e_data_book_view_notify_update_prefiltered_vcard (book_view, id_dbt.data, vcard_dbt.data);
					else
						e_data_book_view_notify_update_vcard (book_view, vcard_dbt.data);
				} else {
					g_free (vcard_dbt.data);
				}

				db_error = dbc->c_get(dbc, &id_dbt, &vcard_dbt, DB_NEXT);
			}

			dbc->c_close (dbc);
			if (db_error && db_error != DB_NOTFOUND)
				g_warning ("e_book_backend_file_search: error building list: %s",
					   db_strerror (db_error));
		}
		else if (db_error == DB_RUNRECOVERY) {
			g_warning ("e_book_backend_file_search: error getting the cursor for %s",
				   bf->priv->filename);
			abort ();
		}

	}
done:
	if (e_flag_is_set (closure->running))
		e_data_book_view_notify_complete (book_view, GNOME_Evolution_Addressbook_Success);

	/* unref the */
	printf("book_view file uref \n");
	e_data_book_view_unref (book_view);

	d(printf ("finished population of book view\n"));

	return NULL;
}

static void
e_book_backend_file_start_book_view (EBookBackend  *backend,
				     EDataBookView *book_view)
{
	FileBackendSearchClosure *closure = init_closure (book_view, E_BOOK_BACKEND_FILE (backend));

	d(printf ("starting book view thread\n"));
	closure->thread = g_thread_create (book_view_thread, book_view, TRUE, NULL);

	e_flag_wait (closure->running);

	/* at this point we know the book view thread is actually running */
	d(printf ("returning from start_book_view\n"));
}

static void
e_book_backend_file_stop_book_view (EBookBackend  *backend,
				    EDataBookView *book_view)
{
	FileBackendSearchClosure *closure = get_closure (book_view);
	gboolean need_join;

	if (!closure)
		return;

	d(printf ("stopping query\n"));
	need_join = e_flag_is_set (closure->running);
	e_flag_clear (closure->running);

	if (need_join)
		g_thread_join (closure->thread);
}

typedef struct {
	DB *db;

	GList *add_cards;
	GList *add_ids;
	GList *mod_cards;
	GList *mod_ids;
	GList *del_ids;
	GList *del_cards;
} EBookBackendFileChangeContext;

static void
e_book_backend_file_changes_foreach_key (const gchar *key, gpointer user_data)
{
	EBookBackendFileChangeContext *ctx = user_data;
	DB      *db = ctx->db;
	DBT     id_dbt, vcard_dbt;
	gint     db_error = 0;

	string_to_dbt (key, &id_dbt);
	memset (&vcard_dbt, 0, sizeof (vcard_dbt));
	vcard_dbt.flags = DB_DBT_MALLOC;

	db_error = db->get (db, NULL, &id_dbt, &vcard_dbt, 0);

	if (db_error != 0) {
		EContact *contact;
		gchar *id = id_dbt.data;
		gchar *vcard_string;

		contact = e_contact_new ();
		e_contact_set (contact, E_CONTACT_UID, id);

		vcard_string = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);

		ctx->del_ids = g_list_append (ctx->del_ids,
					      g_strdup (id));
		ctx->del_cards = g_list_append (ctx->del_cards,
						vcard_string);

		g_object_unref (contact);

		g_free (vcard_dbt.data);
	}
}

static EBookBackendSyncStatus
e_book_backend_file_get_changes (EBookBackendSync *backend,
				 EDataBook *book,
				 guint32 opid,
				 const gchar *change_id,
				 GList **changes_out)
{
	EBookBackendFile *bf = E_BOOK_BACKEND_FILE (backend);
	gint     db_error = 0;
	DBT     id_dbt, vcard_dbt;
	gchar    *filename;
	EDbHash *ehash;
	GList *i, *v;
	DB      *db = bf->priv->file_db;
	DBC *dbc;
	GList *changes = NULL;
	EBookBackendFileChangeContext ctx;
	EBookBackendSyncStatus result;

	memset (&id_dbt, 0, sizeof (id_dbt));
	memset (&vcard_dbt, 0, sizeof (vcard_dbt));

	memset (&ctx, 0, sizeof (ctx));

	ctx.db = db;

	/* Find the changed ids */
	filename = g_strdup_printf ("%s/%s" CHANGES_DB_SUFFIX, bf->priv->dirname, change_id);
	ehash = e_dbhash_new (filename);
	g_free (filename);

	db_error = db->cursor (db, NULL, &dbc, 0);

	if (db_error != 0) {
		g_warning (G_STRLOC ": db->cursor failed with %s", db_strerror (db_error));
	} else {
		db_error = dbc->c_get(dbc, &id_dbt, &vcard_dbt, DB_FIRST);

		while (db_error == 0) {

			/* don't include the version in the list of cards */
			if (id_dbt.size != strlen(E_BOOK_BACKEND_FILE_VERSION_NAME) + 1
			    || strcmp (id_dbt.data, E_BOOK_BACKEND_FILE_VERSION_NAME)) {
				EContact *contact;
				gchar *id = id_dbt.data;
				gchar *vcard_string;

				/* Remove fields the user can't change
				 * and can change without the rest of the
				 * card changing
				 */
				contact = create_contact (id_dbt.data, vcard_dbt.data);

#ifdef notyet
				g_object_set (card, "last_use", NULL, "use_score", 0.0, NULL);
#endif
				vcard_string = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
				g_object_unref (contact);

				/* check what type of change has occurred, if any */
				switch (e_dbhash_compare (ehash, id, vcard_string)) {
				case E_DBHASH_STATUS_SAME:
					g_free(vcard_string);
					break;
				case E_DBHASH_STATUS_NOT_FOUND:
					ctx.add_cards = g_list_append (ctx.add_cards, vcard_string);
					ctx.add_ids = g_list_append (ctx.add_ids, g_strdup(id));
					break;
				case E_DBHASH_STATUS_DIFFERENT:
					ctx.mod_cards = g_list_append (ctx.mod_cards, vcard_string);
					ctx.mod_ids = g_list_append (ctx.mod_ids, g_strdup(id));
					break;
				}
			}

			db_error = dbc->c_get(dbc, &id_dbt, &vcard_dbt, DB_NEXT);
		}
		dbc->c_close (dbc);
	}

	e_dbhash_foreach_key (ehash, (EDbHashFunc)e_book_backend_file_changes_foreach_key, &ctx);

	/* Send the changes */
	if (db_error != DB_NOTFOUND) {
		g_warning ("e_book_backend_file_changes: error building list\n");
		*changes_out = NULL;
		result = db_error_to_status (db_error);
	}
	else {
		/* Update the hash and build our changes list */
		for (i = ctx.add_ids, v = ctx.add_cards; i != NULL; i = i->next, v = v->next) {
			gchar *id = i->data;
			gchar *vcard = v->data;

			e_dbhash_add (ehash, id, vcard);
			changes = g_list_prepend (changes,
						  e_book_backend_change_add_new (vcard));

			g_free (i->data);
			g_free (v->data);
		}
		for (i = ctx.mod_ids, v = ctx.mod_cards; i != NULL; i = i->next, v = v->next) {
			gchar *id = i->data;
			gchar *vcard = v->data;

			e_dbhash_add (ehash, id, vcard);
			changes = g_list_prepend (changes,
						  e_book_backend_change_modify_new (vcard));

			g_free (i->data);
			g_free (v->data);
		}
		for (i = ctx.del_ids, v = ctx.del_cards; i != NULL; i = i->next, v = v->next) {
			gchar *id = i->data;
			gchar *vcard = v->data;

			e_dbhash_remove (ehash, id);

			changes = g_list_prepend (changes,
						  e_book_backend_change_delete_new (vcard));
			g_free (i->data);
			g_free (v->data);
		}

		e_dbhash_write (ehash);

		result = GNOME_Evolution_Addressbook_Success;
		*changes_out = changes;
	}

	e_dbhash_destroy (ehash);

	return GNOME_Evolution_Addressbook_Success;
}

static gchar *
e_book_backend_file_extract_path_from_uri (const gchar *uri)
{
	g_assert (g_ascii_strncasecmp (uri, "file://", 7) == 0);

	return g_filename_from_uri (uri, NULL, NULL);
}

static EBookBackendSyncStatus
e_book_backend_file_authenticate_user (EBookBackendSync *backend,
				       EDataBook *book,
				       guint32 opid,
				       const gchar *user,
				       const gchar *passwd,
				       const gchar *auth_method)
{
	return GNOME_Evolution_Addressbook_Success;
}

static EBookBackendSyncStatus
e_book_backend_file_get_required_fields (EBookBackendSync *backend,
					  EDataBook *book,
					  guint32 opid,
					  GList **fields_out)
{
	GList *fields = NULL;

	fields = g_list_append (fields , g_strdup(e_contact_field_name (E_CONTACT_FILE_AS)));
	*fields_out = fields;
	return GNOME_Evolution_Addressbook_Success;
}

static EBookBackendSyncStatus
e_book_backend_file_get_supported_auth_methods (EBookBackendSync *backend,
						EDataBook *book,
						guint32 opid,
						GList **methods_out)
{
	*methods_out = NULL;
	return GNOME_Evolution_Addressbook_Success;
}

static EBookBackendSyncStatus
e_book_backend_file_get_supported_fields (EBookBackendSync *backend,
					  EDataBook *book,
					  guint32 opid,
					  GList **fields_out)
{
	GList *fields = NULL;
	gint i;

	/* XXX we need a way to say "we support everything", since the
	   file backend does */
	for (i = 1; i < E_CONTACT_FIELD_LAST; i ++)
		fields = g_list_append (fields, g_strdup (e_contact_field_name (i)));

	*fields_out = fields;
	return GNOME_Evolution_Addressbook_Success;
}

/*
** versions:
**
** 0.0 just a list of cards
**
** 0.1 same as 0.0, but with the version tag
**
** 0.2 not a real format upgrade, just a hack to fix broken ids caused
**     by a bug in early betas, but we only need to convert them if
**     the previous version is 0.1, since the bug existed after 0.1
**     came about.
*/
static gboolean
e_book_backend_file_upgrade_db (EBookBackendFile *bf, gchar *old_version)
{
	DB  *db = bf->priv->file_db;
	gint db_error;
	DBT version_name_dbt, version_dbt;

	if (strcmp (old_version, "0.0")
	    && strcmp (old_version, "0.1")) {
		g_warning ("unsupported version '%s' found in PAS backend file\n",
			   old_version);
		return FALSE;
	}

	if (!strcmp (old_version, "0.1")) {
		/* we just loop through all the cards in the db,
                   giving them valid ids if they don't have them */
		DBT  id_dbt, vcard_dbt;
		DBC *dbc;
		gint  card_failed = 0;

		db_error = db->cursor (db, NULL, &dbc, 0);
		if (db_error != 0) {
			g_warning (G_STRLOC ": db->cursor failed with %s", db_strerror (db_error));
			return FALSE;
		}

		memset (&id_dbt, 0, sizeof (id_dbt));
		memset (&vcard_dbt, 0, sizeof (vcard_dbt));

		db_error = dbc->c_get(dbc, &id_dbt, &vcard_dbt, DB_FIRST);

		while (db_error == 0) {
			if (id_dbt.size != strlen(E_BOOK_BACKEND_FILE_VERSION_NAME) + 1
			    || strcmp (id_dbt.data, E_BOOK_BACKEND_FILE_VERSION_NAME)) {
				EContact *contact;

				contact = create_contact (id_dbt.data, vcard_dbt.data);

				/* the cards we're looking for are
				   created with a normal id dbt, but
				   with the id field in the vcard set
				   to something that doesn't match.
				   so, we need to modify the card to
				   have the same id as the the dbt. */
				if (strcmp (id_dbt.data, e_contact_get_const (contact, E_CONTACT_UID))) {
					gchar *vcard;

					e_contact_set (contact, E_CONTACT_UID, id_dbt.data);

					vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
					string_to_dbt (vcard, &vcard_dbt);

					db_error = db->put (db, NULL,
							    &id_dbt, &vcard_dbt, 0);

					g_free (vcard);

					if (db_error != 0)
						card_failed++;
				}

				g_object_unref (contact);
			}

			db_error = dbc->c_get(dbc, &id_dbt, &vcard_dbt, DB_NEXT);
		}

		if (card_failed) {
			g_warning ("failed to update %d cards", card_failed);
			return FALSE;
		}
	}

	string_to_dbt (E_BOOK_BACKEND_FILE_VERSION_NAME, &version_name_dbt);
	string_to_dbt (E_BOOK_BACKEND_FILE_VERSION, &version_dbt);

	db_error = db->put (db, NULL, &version_name_dbt, &version_dbt, 0);
	if (db_error == 0)
		return TRUE;
	else
		return FALSE;
}

static gboolean
e_book_backend_file_maybe_upgrade_db (EBookBackendFile *bf)
{
	DB   *db = bf->priv->file_db;
	DBT  version_name_dbt, version_dbt;
	gint  db_error;
	gchar *version;
	gboolean ret_val = TRUE;

	string_to_dbt (E_BOOK_BACKEND_FILE_VERSION_NAME, &version_name_dbt);
	memset (&version_dbt, 0, sizeof (version_dbt));
	version_dbt.flags = DB_DBT_MALLOC;

	db_error = db->get (db, NULL, &version_name_dbt, &version_dbt, 0);
	if (db_error == 0) {
		/* success */
		version = version_dbt.data;
	}
	else {
		/* key was not in file */
		version = g_strdup ("0.0");
	}

	if (strcmp (version, E_BOOK_BACKEND_FILE_VERSION))
		ret_val = e_book_backend_file_upgrade_db (bf, version);

	g_free (version);

	return ret_val;
}

#ifdef CREATE_DEFAULT_VCARD
# include <libedata-book/ximian-vcard.h>
#endif

static void
#if DB_VERSION_MAJOR == 4 && DB_VERSION_MINOR >= 3
file_errcall (const DB_ENV *env, const gchar *buf1, const gchar *buf2)
#else
file_errcall (const gchar *buf1, gchar *buf2)
#endif
{
	g_warning ("libdb error: %s", buf2);
}

static GNOME_Evolution_Addressbook_CallStatus
e_book_backend_file_load_source (EBookBackend           *backend,
				 ESource                *source,
				 gboolean                only_if_exists)
{
	EBookBackendFile *bf = E_BOOK_BACKEND_FILE (backend);
	gchar           *dirname, *filename;
	gboolean        writable = FALSE;
	gint             db_error;
	DB *db;
	DB_ENV *env;
	time_t db_mtime;
	struct stat sb;
	gchar *uri;

	uri = e_source_get_uri (source);

	dirname = e_book_backend_file_extract_path_from_uri (uri);
	filename = g_build_filename (dirname, "addressbook.db", NULL);
	g_free (uri);

	db_error = e_db3_utils_maybe_recover (filename);
	if (db_error != 0) {
		g_warning ("db recovery failed with %s", db_strerror (db_error));
		g_free (dirname);
		g_free (filename);
		return db_error_to_status (db_error);
	}

	G_LOCK (global_env);
	if (global_env.ref_count > 0) {
		env = global_env.env;
		global_env.ref_count++;
	} else {
		db_error = db_env_create (&env, 0);
		if (db_error != 0) {
			g_warning ("db_env_create failed with %s", db_strerror (db_error));
			G_UNLOCK (global_env);
			g_free (dirname);
			g_free (filename);
			return db_error_to_status (db_error);
		}

		env->set_errcall (env, file_errcall);

		/* Set the allocation routines to the non-aborting GLib functions */
		env->set_alloc (env, (gpointer (*)(gsize))g_try_malloc,
				(gpointer (*)(gpointer , gsize))g_try_realloc,
				g_free);

		db_error = (*env->open) (env, NULL, DB_CREATE | DB_INIT_MPOOL | DB_PRIVATE | DB_THREAD, 0);
		if (db_error != 0) {
			env->close(env, 0);
			g_warning ("db_env_open failed with %s", db_strerror (db_error));
			G_UNLOCK (global_env);
			g_free (dirname);
			g_free (filename);
			return db_error_to_status (db_error);
		}

		global_env.env = env;
		global_env.ref_count = 1;
	}
	G_UNLOCK (global_env);

	bf->priv->env = env;

	db_error = db_create (&db, env, 0);
	if (db_error != 0) {
		g_warning ("db_create failed with %s", db_strerror (db_error));
		g_free (dirname);
		g_free (filename);
		return db_error_to_status (db_error);
	}

	db_error = (*db->open) (db, NULL, filename, NULL, DB_HASH, DB_THREAD, 0666);

	if (db_error == DB_OLD_VERSION) {
		db_error = e_db3_utils_upgrade_format (filename);

		if (db_error != 0) {
			g_warning ("db format upgrade failed with %s", db_strerror (db_error));
			g_free (dirname);
			g_free (filename);
			return db_error_to_status (db_error);
		}

		db->close (db, 0);
		db_error = db_create (&db, env, 0);
		if (db_error != 0) {
			g_warning ("db_create failed with %s", db_strerror (db_error));
			g_free (dirname);
			g_free (filename);
			return db_error_to_status (db_error);
		}

		db_error = (*db->open) (db, NULL, filename, NULL, DB_HASH, DB_THREAD, 0666);
	}

	if (db_error == 0) {
		writable = TRUE;
	} else {
		db->close (db, 0);
		db_error = db_create (&db, env, 0);
		if (db_error != 0) {
			g_warning ("db_create failed with %s", db_strerror (db_error));
			g_free (dirname);
			g_free (filename);
			return db_error_to_status (db_error);
		}

		db_error = (*db->open) (db, NULL, filename, NULL, DB_HASH, DB_RDONLY | DB_THREAD, 0666);

		if (db_error != 0 && !only_if_exists) {
			gint rv;

			/* the database didn't exist, so we create the
			   directory then the .db */
			db->close (db, 0);
			rv = g_mkdir_with_parents (dirname, 0777);
			if (rv == -1 && errno != EEXIST) {
				g_warning ("failed to make directory %s: %s", dirname, g_strerror (errno));
				g_free (dirname);
				g_free (filename);
				if (errno == EACCES || errno == EPERM)
					return GNOME_Evolution_Addressbook_PermissionDenied;
				else
					return GNOME_Evolution_Addressbook_OtherError;
			}

			db_error = db_create (&db, env, 0);
			if (db_error != 0) {
				g_warning ("db_create failed with %s", db_strerror (db_error));
				g_free (dirname);
				g_free (filename);
				return db_error_to_status (db_error);
			}

			db_error = (*db->open) (db, NULL, filename, NULL, DB_HASH, DB_CREATE | DB_THREAD, 0666);
			if (db_error != 0) {
				db->close (db, 0);
				g_warning ("db->open (... DB_CREATE ...) failed with %s", db_strerror (db_error));
			}
			else {
#ifdef CREATE_DEFAULT_VCARD
				EContact *contact = NULL;
				EBookBackendSyncStatus status;

				status = do_create (bf, XIMIAN_VCARD, &contact);
				if (status != GNOME_Evolution_Addressbook_Success)
					g_warning ("Cannot create default contact: %d", status);
				if (contact)
					g_object_unref (contact);
#endif

				writable = TRUE;
			}
		}
	}

	bf->priv->file_db = db;

	if (db_error != 0) {
		bf->priv->file_db = NULL;
		g_free (dirname);
		g_free (filename);
		return db_error_to_status (db_error);
	}

	if (!e_book_backend_file_maybe_upgrade_db (bf)) {
		db->close (db, 0);
		bf->priv->file_db = NULL;
		g_warning ("e_book_backend_file_maybe_upgrade_db failed");
		g_free (dirname);
		g_free (filename);
		return GNOME_Evolution_Addressbook_OtherError;
	}

	g_free (bf->priv->dirname);
	g_free (bf->priv->filename);
	bf->priv->dirname = dirname;
	bf->priv->filename = filename;

	if (g_stat (bf->priv->filename, &sb) == -1) {
		db->close (db, 0);
		bf->priv->file_db = NULL;
		g_warning ("stat(%s) failed", bf->priv->filename);
		return GNOME_Evolution_Addressbook_OtherError;
	}
	db_mtime = sb.st_mtime;

	g_free (bf->priv->summary_filename);
	bf->priv->summary_filename = g_strconcat (bf->priv->filename, ".summary", NULL);
	bf->priv->summary = e_book_backend_summary_new (bf->priv->summary_filename, SUMMARY_FLUSH_TIMEOUT);

	if (e_book_backend_summary_is_up_to_date (bf->priv->summary, db_mtime) == FALSE
	    || e_book_backend_summary_load (bf->priv->summary) == FALSE ) {
		if (!bf->priv->summary || !build_summary (bf->priv)) {
			g_warning ("Failed to build summary for an address book %s", bf->priv->filename);
			return GNOME_Evolution_Addressbook_OtherError;
		}
	}

	e_book_backend_set_is_loaded (backend, TRUE);
	e_book_backend_set_is_writable (backend, writable);
	return GNOME_Evolution_Addressbook_Success;
}

static gboolean
select_changes (const gchar *name)
{
	gchar *p;

	if (strlen (name) < strlen (CHANGES_DB_SUFFIX))
		return FALSE;

	p = strstr (name, CHANGES_DB_SUFFIX);
	if (!p)
		return FALSE;

	if (strlen (p) != strlen (CHANGES_DB_SUFFIX))
		return FALSE;

	return TRUE;
}

static EBookBackendSyncStatus
e_book_backend_file_remove (EBookBackendSync *backend,
			    EDataBook        *book,
			    guint32           opid)
{
	EBookBackendFile *bf = E_BOOK_BACKEND_FILE (backend);
	GDir *dir;

	if (-1 == g_unlink (bf->priv->filename)) {
		if (errno == EACCES || errno == EPERM)
			return GNOME_Evolution_Addressbook_PermissionDenied;
		else
			return GNOME_Evolution_Addressbook_OtherError;
	}

	/* unref the summary before we remove the file so it's not written out again */
	g_object_unref (bf->priv->summary);
	bf->priv->summary = NULL;
	if (-1 == g_unlink (bf->priv->summary_filename))
		g_warning ("failed to remove summary file `%s`: %s", bf->priv->summary_filename, g_strerror (errno));

	dir = g_dir_open (bf->priv->dirname, 0, NULL);
	if (dir) {
		const gchar *name;

		while ((name = g_dir_read_name (dir))) {
			if (select_changes (name)) {
				gchar *full_path = g_build_filename (bf->priv->dirname, name, NULL);
				if (-1 == g_unlink (full_path)) {
					g_warning ("failed to remove change db `%s': %s", full_path, g_strerror (errno));
				}
				g_free (full_path);
			}
		}

		g_dir_close (dir);
	}

	if (-1 == g_rmdir (bf->priv->dirname))
		g_warning ("failed to remove directory `%s`: %s", bf->priv->dirname, g_strerror (errno));

	/* we may not have actually succeeded in removing the
	   backend's files/dirs, but there's nothing we can do about
	   it here..  the only time we should return failure is if we
	   failed to remove the actual data.  a failure should mean
	   that the addressbook is still valid */
	return GNOME_Evolution_Addressbook_Success;
}

static gchar *
e_book_backend_file_get_static_capabilities (EBookBackend *backend)
{
	return g_strdup("local,do-initial-query,bulk-removes,contact-lists");
}

static GNOME_Evolution_Addressbook_CallStatus
e_book_backend_file_cancel_operation (EBookBackend *backend, EDataBook *book)
{
	return GNOME_Evolution_Addressbook_CouldNotCancel;
}
static void
e_book_backend_file_set_mode (EBookBackend *backend,  GNOME_Evolution_Addressbook_BookMode mode)
{
	if (e_book_backend_is_loaded (backend)) {
		e_book_backend_notify_writable (backend, TRUE);
		e_book_backend_notify_connection_status (backend, TRUE);
	}
}

static void
e_book_backend_file_sync (EBookBackend *backend)
{
	EBookBackendFile *bf = E_BOOK_BACKEND_FILE (backend);
	gint db_error;

	g_return_if_fail (bf != NULL);

	if (bf->priv->file_db) {
		db_error = bf->priv->file_db->sync (bf->priv->file_db, 0);
		if (db_error != 0)
			g_warning (G_STRLOC ": db->sync failed with %s", db_strerror (db_error));
	}
}

static gboolean
e_book_backend_file_construct (EBookBackendFile *backend)
{
	g_assert (backend != NULL);
	g_assert (E_IS_BOOK_BACKEND_FILE (backend));

	if (! e_book_backend_construct (E_BOOK_BACKEND (backend)))
		return FALSE;

	return TRUE;
}

/**
 * e_book_backend_file_new:
 */
EBookBackend *
e_book_backend_file_new (void)
{
	EBookBackendFile *backend;

	backend = g_object_new (E_TYPE_BOOK_BACKEND_FILE, NULL);

	if (! e_book_backend_file_construct (backend)) {
		g_object_unref (backend);

		return NULL;
	}

	return E_BOOK_BACKEND (backend);
}

static void
e_book_backend_file_dispose (GObject *object)
{
	EBookBackendFile *bf;

	bf = E_BOOK_BACKEND_FILE (object);

	if (bf->priv->file_db) {
		bf->priv->file_db->close (bf->priv->file_db, 0);
		bf->priv->file_db = NULL;
	}

	G_LOCK (global_env);
	global_env.ref_count--;
	if (global_env.ref_count == 0) {
		global_env.env->close (global_env.env, 0);
		global_env.env = NULL;
	}
	G_UNLOCK (global_env);

	if (bf->priv->summary) {
		g_object_unref (bf->priv->summary);
		bf->priv->summary = NULL;
	}

	G_OBJECT_CLASS (e_book_backend_file_parent_class)->dispose (object);
}

static void
e_book_backend_file_finalize (GObject *object)
{
	EBookBackendFile *bf;

	bf = E_BOOK_BACKEND_FILE (object);

	g_free (bf->priv->filename);
	g_free (bf->priv->dirname);
	g_free (bf->priv->summary_filename);

	g_free (bf->priv);

	G_OBJECT_CLASS (e_book_backend_file_parent_class)->finalize (object);
}

#ifdef G_OS_WIN32
/* Avoid compiler warning by providing a function with exactly the
 * prototype that db_env_set_func_open() wants for the open method.
 */

static gint
my_open (const gchar *name, gint oflag, ...)
{
	gint mode = 0;

	if (oflag & O_CREAT) {
		va_list arg;
		va_start (arg, oflag);
		mode = va_arg (arg, gint);
		va_end (arg);
	}

	return g_open (name, oflag, mode);
}

gint
my_rename (const gchar *oldname, const gchar *newname)
{
	return g_rename (oldname, newname);
}

gint
my_exists (const gchar *name, gint *isdirp)
{
	if (!g_file_test (name, G_FILE_TEST_EXISTS))
		return ENOENT;
	if (isdirp != NULL)
		*isdirp = g_file_test (name, G_FILE_TEST_IS_DIR);
	return 0;
}

gint
my_unlink (const gchar *name)
{
	return g_unlink (name);
}

#endif

static void
e_book_backend_file_class_init (EBookBackendFileClass *klass)
{
	GObjectClass    *object_class = G_OBJECT_CLASS (klass);
	EBookBackendSyncClass *sync_class;
	EBookBackendClass *backend_class;

	sync_class = E_BOOK_BACKEND_SYNC_CLASS (klass);
	backend_class = E_BOOK_BACKEND_CLASS (klass);

	/* Set the virtual methods. */
	backend_class->load_source			= e_book_backend_file_load_source;
	backend_class->get_static_capabilities		= e_book_backend_file_get_static_capabilities;
	backend_class->start_book_view			= e_book_backend_file_start_book_view;
	backend_class->stop_book_view			= e_book_backend_file_stop_book_view;
	backend_class->cancel_operation			= e_book_backend_file_cancel_operation;
	backend_class->set_mode				= e_book_backend_file_set_mode;
	backend_class->sync				= e_book_backend_file_sync;
	sync_class->remove_sync				= e_book_backend_file_remove;
	sync_class->create_contact_sync			= e_book_backend_file_create_contact;
	sync_class->remove_contacts_sync		= e_book_backend_file_remove_contacts;
	sync_class->modify_contact_sync			= e_book_backend_file_modify_contact;
	sync_class->get_contact_sync			= e_book_backend_file_get_contact;
	sync_class->get_contact_list_sync		= e_book_backend_file_get_contact_list;
	sync_class->get_changes_sync			= e_book_backend_file_get_changes;
	sync_class->authenticate_user_sync		= e_book_backend_file_authenticate_user;
	sync_class->get_supported_auth_methods_sync	= e_book_backend_file_get_supported_auth_methods;
	sync_class->get_supported_fields_sync		= e_book_backend_file_get_supported_fields;
	sync_class->get_required_fields_sync		= e_book_backend_file_get_required_fields;

	object_class->dispose = e_book_backend_file_dispose;
	object_class->finalize = e_book_backend_file_finalize;

#ifdef G_OS_WIN32
	/* Use the gstdio wrappers to open, check, rename and unlink
	 * files from libdb.
	 */
	db_env_set_func_open (my_open);
	db_env_set_func_close (close);
	db_env_set_func_exists (my_exists);
	db_env_set_func_rename (my_rename);
	db_env_set_func_unlink (my_unlink);
#endif
}

static void
e_book_backend_file_init (EBookBackendFile *backend)
{
	EBookBackendFilePrivate *priv;

	priv             = g_new0 (EBookBackendFilePrivate, 1);

	backend->priv = priv;
}
