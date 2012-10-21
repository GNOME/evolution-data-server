/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-book-backend-vcf.c - VCF contact backend.
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
 * Authors: Chris Toshok <toshok@ximian.com>
 */

#include <config.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/time.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>

#include "e-book-backend-vcf.h"
#include "e-source-vcf.h"

#define E_BOOK_BACKEND_VCF_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_BOOK_BACKEND_VCF, EBookBackendVCFPrivate))

#define PAS_ID_PREFIX "pas-id-"
#define FILE_FLUSH_TIMEOUT 5000

#define d(x)

#define EDB_ERROR(_code) e_data_book_create_error (E_DATA_BOOK_STATUS_ ## _code, NULL)
#define EDB_ERROR_EX(_code, _msg) e_data_book_create_error (E_DATA_BOOK_STATUS_ ## _code, _msg)

G_DEFINE_TYPE (EBookBackendVCF, e_book_backend_vcf, E_TYPE_BOOK_BACKEND_SYNC)

typedef struct _EBookBackendVCFBookView EBookBackendVCFBookView;
typedef struct _EBookBackendVCFSearchContext EBookBackendVCFSearchContext;

struct _EBookBackendVCFPrivate {
	gchar       *filename;
	GMutex     *mutex;
	GHashTable *contacts;
	GList      *contact_list;
	gboolean    dirty;
	gint         flush_timeout_tag;
};

static gchar *
e_book_backend_vcf_create_unique_id (void)
{
	/* use a 32 counter and the 32 bit timestamp to make an id.
	 * it's doubtful 2^32 id's will be created in a second, so we
	 * should be okay. */
	static guint c = 0;
	return g_strdup_printf (PAS_ID_PREFIX "%08lX%08X", time (NULL), c++);
}

static void
insert_contact (EBookBackendVCF *vcf,
                gchar *vcard)
{
	EContact *contact = e_contact_new_from_vcard (vcard);
	gchar *id;

	id = e_contact_get (contact, E_CONTACT_UID);
	if (id) {
		gchar *vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);

		vcf->priv->contact_list = g_list_prepend (vcf->priv->contact_list, vcard);

		g_hash_table_insert (
			vcf->priv->contacts,
			id,
			vcf->priv->contact_list);
	}
}

static void
load_file (EBookBackendVCF *vcf,
           gint fd)
{
	FILE *fp;
	GString *str;
	gchar buf[1024];

	fp = fdopen (fd, "rb");
	if (!fp) {
		close (fd); /* callers depend on fd being closed by this function */
		g_warning ("failed to open `%s' for reading", vcf->priv->filename);
		return;
	}

	str = g_string_new ("");

	while (fgets (buf, sizeof (buf), fp)) {
		if (!strcmp (buf, "\r\n")) {
			/* if the string has accumulated some stuff, create a contact for it and start over */
			if (str->len) {
				insert_contact (vcf, str->str);
				g_string_assign (str, "");
			}
		}
		else {
			g_string_append (str, buf);
		}
	}
	if (str->len) {
		insert_contact (vcf, str->str);
	}

	g_string_free (str, TRUE);

	fclose (fp);
}

static gboolean
save_file (EBookBackendVCF *vcf)
{
	gboolean retv = FALSE;
	GList *l;
	gchar *new_path;
	gint fd, rv;

	g_warning ("EBookBackendVCF flushing file to disk");

	g_mutex_lock (vcf->priv->mutex);

	new_path = g_strdup_printf ("%s.new", vcf->priv->filename);

	fd = g_open (new_path, O_CREAT | O_TRUNC | O_WRONLY | O_BINARY, 0666);
	if (fd == -1) {
		g_warning ("write failed.  could not open output file\n");
		goto out;
	}

	for (l = vcf->priv->contact_list; l; l = l->next) {
		gchar *vcard_str = l->data;
		gint len = strlen (vcard_str);

		rv = write (fd, vcard_str, len);

		if (rv < len) {
			/* XXX */
			g_warning ("write failed.  we need to handle short writes\n");
			g_unlink (new_path);
			goto out;
		}

		rv = write (fd, "\r\n\r\n", 4);
		if (rv < 4) {
			/* XXX */
			g_warning ("write failed.  we need to handle short writes\n");
			g_unlink (new_path);
			goto out;
		}
	}

	if (0 > g_rename (new_path, vcf->priv->filename)) {
		g_warning ("Failed to rename %s: %s\n", vcf->priv->filename, g_strerror (errno));
		g_unlink (new_path);
		goto out;
	}
	retv = TRUE;

out:
	if (fd != -1)
		close (fd);
	g_free (new_path);
	vcf->priv->dirty = !retv;
	g_mutex_unlock (vcf->priv->mutex);

	return retv;
}

static gboolean
vcf_flush_file (gpointer data)
{
	EBookBackendVCF *bvcf = E_BOOK_BACKEND_VCF (data);

	if (!bvcf->priv->dirty) {
		bvcf->priv->flush_timeout_tag = 0;
		return FALSE;
	}

	if (!save_file (bvcf)) {
		g_warning ("failed to flush the .vcf file to disk, will try again next timeout");
		return TRUE;
	}

	bvcf->priv->flush_timeout_tag = 0;
	return FALSE;
}

static void
set_revision (EContact *contact)
{
	gchar time_string[100] = {0};
	const struct tm *tm = NULL;
	time_t t;

	t = time (NULL);
	tm = gmtime (&t);
	if (tm)
		strftime (time_string, 100, "%Y-%m-%dT%H:%M:%SZ", tm);
	e_contact_set (contact, E_CONTACT_REV, time_string);
}

static EContact *
do_create (EBookBackendVCF *bvcf,
           const gchar *vcard_req,
           gboolean dirty_the_file)
{
	gchar           *id;
	EContact       *contact;
	gchar           *vcard;
	const gchar     *rev;

	/* at the very least we need the unique_id generation to be
	 * protected by the lock, even if the actual vcard parsing
	 * isn't. */
	g_mutex_lock (bvcf->priv->mutex);
	id = e_book_backend_vcf_create_unique_id ();

	contact = e_contact_new_from_vcard_with_uid (vcard_req, id);
	g_free (id);

	rev = e_contact_get_const (contact,  E_CONTACT_REV);
	if (!(rev && *rev))
		set_revision (contact);

	vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);

	insert_contact (bvcf, vcard);

	if (dirty_the_file) {
		bvcf->priv->dirty = TRUE;

		if (!bvcf->priv->flush_timeout_tag)
			bvcf->priv->flush_timeout_tag = g_timeout_add (
				FILE_FLUSH_TIMEOUT,
				vcf_flush_file, bvcf);
	}

	g_mutex_unlock (bvcf->priv->mutex);

	return contact;
}

static void
e_book_backend_vcf_create_contacts (EBookBackendSync *backend,
                                   EDataBook *book,
                                   GCancellable *cancellable,
                                   const GSList *vcards,
                                   GSList **added_contacts,
                                   GError **perror)
{
	EContact *contact = NULL;
	EBookBackendVCF *bvcf = E_BOOK_BACKEND_VCF (backend);
	const gchar *vcard = vcards->data;

	/* We make the assumption that the vCard list we're passed is always exactly one element long, since we haven't specified "bulk-adds"
	 * in our static capability list. */
	if (vcards->next != NULL) {
		g_propagate_error (
			perror,
			EDB_ERROR_EX (NOT_SUPPORTED,
			_("The backend does not support bulk additions")));
		return;
	}

	contact = do_create (bvcf, vcard, TRUE);
	if (added_contacts) {
		*added_contacts = g_slist_append (*added_contacts, contact);
	} else {
		/* XXX need a different call status for this case, i
		 * think */
		g_propagate_error (perror, EDB_ERROR (CONTACT_NOT_FOUND));
	}
}

static void
e_book_backend_vcf_remove_contacts (EBookBackendSync *backend,
                                    EDataBook *book,
                                    GCancellable *cancellable,
                                    const GSList *id_list,
                                    GSList **ids,
                                    GError **perror)
{
	/* FIXME: make this handle bulk deletes like the file backend does */
	EBookBackendVCF *bvcf = E_BOOK_BACKEND_VCF (backend);
	const gchar *id = id_list->data;
	GList *elem;

	/* We make the assumption that the ID list we're passed is always exactly one element long, since we haven't specified "bulk-removes"
	 * in our static capability list. */
	if (id_list->next != NULL) {
		g_propagate_error (
			perror,
			EDB_ERROR_EX (NOT_SUPPORTED,
			_("The backend does not support bulk removals")));
		return;
	}

	g_mutex_lock (bvcf->priv->mutex);
	elem = g_hash_table_lookup (bvcf->priv->contacts, id);
	if (!elem) {
		g_mutex_unlock (bvcf->priv->mutex);
		g_propagate_error (perror, EDB_ERROR (CONTACT_NOT_FOUND));
		return;
	}

	if (!g_hash_table_remove (bvcf->priv->contacts, id)) {
		g_mutex_unlock (bvcf->priv->mutex);
		g_propagate_error (perror, EDB_ERROR (CONTACT_NOT_FOUND));
		return;
	}

	g_free (elem->data);
	bvcf->priv->contact_list = g_list_remove_link (bvcf->priv->contact_list, elem);

	bvcf->priv->dirty = TRUE;
	if (!bvcf->priv->flush_timeout_tag)
		bvcf->priv->flush_timeout_tag = g_timeout_add (
			FILE_FLUSH_TIMEOUT,
			vcf_flush_file, bvcf);
	g_mutex_unlock (bvcf->priv->mutex);

	*ids = g_slist_append (*ids, g_strdup (id));
}

static void
e_book_backend_vcf_modify_contacts (EBookBackendSync *backend,
                                    EDataBook *book,
                                    GCancellable *cancellable,
                                    const GSList *vcards,
                                    GSList **modified_contacts,
                                    GError **perror)
{
	EBookBackendVCF *bvcf = E_BOOK_BACKEND_VCF (backend);
	GList *elem;
	const gchar *id;
	EContact *contact;

	/* We make the assumption that the vCard list we're passed is always exactly one element long, since we haven't specified "bulk-modifies"
	 * in our static capability list. */
	if (vcards->next != NULL) {
		g_propagate_error (
			perror,
			EDB_ERROR_EX (NOT_SUPPORTED,
			_("The backend does not support bulk modifications")));
		return;
	}

	/* create a new ecard from the request data */
	contact = e_contact_new_from_vcard (vcards->data);
	id = e_contact_get_const (contact, E_CONTACT_UID);

	g_mutex_lock (bvcf->priv->mutex);
	elem = g_hash_table_lookup (bvcf->priv->contacts, id);
	if (!elem) {
		g_mutex_unlock (bvcf->priv->mutex);
		g_propagate_error (perror, EDB_ERROR (CONTACT_NOT_FOUND));
		return;
	}

	g_free (elem->data);
	elem->data = g_strdup (vcards->data);
	bvcf->priv->dirty = TRUE;
	if (!bvcf->priv->flush_timeout_tag)
		bvcf->priv->flush_timeout_tag = g_timeout_add (
			FILE_FLUSH_TIMEOUT,
			vcf_flush_file, bvcf);
	g_mutex_unlock (bvcf->priv->mutex);

	*modified_contacts = g_slist_append (*modified_contacts, contact);
}

static void
e_book_backend_vcf_get_contact (EBookBackendSync *backend,
                                EDataBook *book,
                                GCancellable *cancellable,
                                const gchar *id,
                                gchar **vcard,
                                GError **perror)
{
	EBookBackendVCF *bvcf = E_BOOK_BACKEND_VCF (backend);
	GList *elem;

	elem = g_hash_table_lookup (bvcf->priv->contacts, id);

	if (elem) {
		*vcard = g_strdup (elem->data);
	} else {
		*vcard = NULL;
		g_propagate_error (perror, EDB_ERROR (CONTACT_NOT_FOUND));
	}
}

typedef struct {
	EBookBackendVCF      *bvcf;
	gboolean            search_needed;
	EBookBackendSExp *card_sexp;
	GSList              *list;
} GetContactListClosure;

static void
foreach_get_contact_compare (gchar *vcard_string,
                             GetContactListClosure *closure)
{
	if ((!closure->search_needed) || e_book_backend_sexp_match_vcard  (closure->card_sexp, vcard_string)) {
		closure->list = g_slist_append (closure->list, g_strdup (vcard_string));
	}
}

static void
e_book_backend_vcf_get_contact_list (EBookBackendSync *backend,
                                     EDataBook *book,
                                     GCancellable *cancellable,
                                     const gchar *query,
                                     GSList **contacts,
                                     GError **perror)
{
	EBookBackendVCF *bvcf = E_BOOK_BACKEND_VCF (backend);
	const gchar *search = query;
	GetContactListClosure closure;

	closure.bvcf = bvcf;
	closure.search_needed = strcmp (search, "(contains \"x-evolution-any-field\" \"\")");
	closure.card_sexp = e_book_backend_sexp_new (search);
	closure.list = NULL;

	g_list_foreach (bvcf->priv->contact_list, (GFunc) foreach_get_contact_compare, &closure);

	g_object_unref (closure.card_sexp);

	*contacts = closure.list;
}

typedef struct {
	EBookBackendVCF *bvcf;
	EDataBookView *view;
	GThread *thread;
	EFlag *running;
} VCFBackendSearchClosure;

static void
closure_destroy (VCFBackendSearchClosure *closure)
{
	d (printf ("destroying search closure\n"));
	e_flag_free (closure->running);
	g_free (closure);
}

static VCFBackendSearchClosure *
init_closure (EDataBookView *book_view,
              EBookBackendVCF *bvcf)
{
	VCFBackendSearchClosure *closure = g_new (VCFBackendSearchClosure, 1);

	closure->bvcf = bvcf;
	closure->view = book_view;
	closure->thread = NULL;
	closure->running = e_flag_new ();

	g_object_set_data_full (
		G_OBJECT (book_view),
		"EBookBackendVCF.BookView::closure",
		closure, (GDestroyNotify) closure_destroy);

	return closure;
}

static VCFBackendSearchClosure *
get_closure (EDataBookView *book_view)
{
	return g_object_get_data (
		G_OBJECT (book_view),
		"EBookBackendVCF.BookView::closure");
}

static gpointer
book_view_thread (gpointer data)
{
	EDataBookView *book_view = data;
	VCFBackendSearchClosure *closure = get_closure (book_view);
	const gchar *query;
	GList *l;

	/* ref the book view because it'll be removed and unrefed
	 * when/if it's stopped */
	e_data_book_view_ref (book_view);

	query = e_data_book_view_get_card_query (book_view);

	if ( !strcmp (query, "(contains \"x-evolution-any-field\" \"\")"))
		e_data_book_view_notify_progress (book_view, -1, _("Loading..."));
	else
		e_data_book_view_notify_progress (book_view, -1, _("Searching..."));

	d (printf ("signalling parent thread\n"));
	e_flag_set (closure->running);

	for (l = closure->bvcf->priv->contact_list; l; l = l->next) {
		gchar *vcard_string = l->data;
		EContact *contact = e_contact_new_from_vcard (vcard_string);
		e_data_book_view_notify_update (closure->view, contact);
		g_object_unref (contact);

		if (!e_flag_is_set (closure->running))
			break;
	}

	if (e_flag_is_set (closure->running))
		e_data_book_view_notify_complete (closure->view, NULL /* Success */);

	/* unref the book view */
	e_data_book_view_unref (book_view);

	d (printf ("finished initial population of book view\n"));

	return NULL;
}

static void
e_book_backend_vcf_start_book_view (EBookBackend *backend,
                                    EDataBookView *book_view)
{
	VCFBackendSearchClosure *closure = init_closure (book_view, E_BOOK_BACKEND_VCF (backend));

	d (printf ("starting book view thread\n"));
	closure->thread = g_thread_create (book_view_thread, book_view, TRUE, NULL);

	e_flag_wait (closure->running);

	/* at this point we know the book view thread is actually running */
	d (printf ("returning from start_book_view\n"));

}

static void
e_book_backend_vcf_stop_book_view (EBookBackend *backend,
                                   EDataBookView *book_view)
{
	VCFBackendSearchClosure *closure = get_closure (book_view);
	gboolean need_join;

	d (printf ("stopping query\n"));
	need_join = e_flag_is_set (closure->running);
	e_flag_clear (closure->running);

	if (need_join)
		g_thread_join (closure->thread);
}

#ifdef CREATE_DEFAULT_VCARD
# include <libedata-book/ximian-vcard.h>
#endif

static void
e_book_backend_vcf_open (EBookBackendSync *backend,
                         EDataBook *book,
                         GCancellable *cancellable,
                         gboolean only_if_exists,
                         GError **perror)
{
	EBookBackendVCF *bvcf = E_BOOK_BACKEND_VCF (backend);
	ESource *source;
	ESourceVCF *vcf_extension;
	const gchar *extension_name;
	gchar *dirname;
	gboolean readonly = TRUE;
	gint fd;

	source = e_backend_get_source (E_BACKEND (backend));
	extension_name = E_SOURCE_EXTENSION_VCF_BACKEND;
	vcf_extension = e_source_get_extension (source, extension_name);

	dirname = e_source_vcf_dup_path (vcf_extension);
	bvcf->priv->filename = g_build_filename (dirname, "addressbook.vcf", NULL);

	fd = g_open (bvcf->priv->filename, O_RDWR | O_BINARY, 0);

	bvcf->priv->contacts = g_hash_table_new_full (
		g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) NULL);

	if (fd != -1) {
		readonly = FALSE;
	} else {
		fd = g_open (bvcf->priv->filename, O_RDONLY | O_BINARY, 0);

		if (fd == -1 && !only_if_exists) {
			gint rv;

			/* the database didn't exist, so we create the
			 * directory then the .vcf file */
			rv = g_mkdir_with_parents (dirname, 0700);
			if (rv == -1 && errno != EEXIST) {
				g_warning ("failed to make directory %s: %s", dirname, g_strerror (errno));
				if (errno == EACCES || errno == EPERM) {
					g_propagate_error (perror, EDB_ERROR (PERMISSION_DENIED));
				} else {
					g_propagate_error (perror, e_data_book_create_error_fmt (E_DATA_BOOK_STATUS_OTHER_ERROR, _("Failed to make directory %s: %s"), dirname, g_strerror (errno)));
				}
				goto exit;
			}

			fd = g_open (bvcf->priv->filename, O_CREAT | O_BINARY, 0666);

			if (fd != -1) {
#ifdef CREATE_DEFAULT_VCARD
				EContact *contact;

				contact = do_create (bvcf, XIMIAN_VCARD, FALSE);
				save_file (bvcf);

				/* XXX check errors here */
				g_object_unref (contact);
#endif

				readonly = FALSE;
			}
		}
	}

	if (fd == -1) {
		g_warning ("Failed to open addressbook at `%s'", dirname);
		g_warning ("error == %s", g_strerror (errno));
		g_propagate_error (
			perror, e_data_book_create_error_fmt (
			E_DATA_BOOK_STATUS_OTHER_ERROR,
			_("Failed to open addressbook at '%s': %s"),
			dirname, g_strerror (errno)));
		goto exit;
	}

	load_file (bvcf, fd);

	e_book_backend_notify_readonly (E_BOOK_BACKEND (backend), readonly);
	e_book_backend_notify_online (E_BOOK_BACKEND (backend), TRUE);
	e_book_backend_notify_opened (E_BOOK_BACKEND (backend), NULL);

exit:
	g_free (dirname);
}

static gboolean
e_book_backend_vcf_get_backend_property (EBookBackendSync *backend,
                                         EDataBook *book,
                                         GCancellable *cancellable,
                                         const gchar *prop_name,
                                         gchar **prop_value,
                                         GError **error)
{
	gboolean processed = TRUE;

	g_return_val_if_fail (prop_name != NULL, FALSE);
	g_return_val_if_fail (prop_value != NULL, FALSE);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		*prop_value = g_strdup ("local,do-initial-query,contact-lists");
	} else if (g_str_equal (prop_name, BOOK_BACKEND_PROPERTY_REQUIRED_FIELDS)) {
		*prop_value = g_strdup (e_contact_field_name (E_CONTACT_FILE_AS));
	} else if (g_str_equal (prop_name, BOOK_BACKEND_PROPERTY_SUPPORTED_FIELDS)) {
		GSList *fields = NULL;
		gint i;

		/* XXX we need a way to say "we support everything", since the
		 * vcf backend does */
		for (i = 1; i < E_CONTACT_FIELD_LAST; i++)
			fields = g_slist_append (fields, (gpointer) e_contact_field_name (i));

		*prop_value = e_data_book_string_slist_to_comma_string (fields);
		g_slist_free (fields);
	} else if (g_str_equal (prop_name, BOOK_BACKEND_PROPERTY_SUPPORTED_AUTH_METHODS)) {
		*prop_value = NULL;
	} else {
		processed = FALSE;
	}

	return processed;
}

static void
e_book_backend_vcf_notify_online_cb (EBookBackend *backend,
                                     GParamSpec *pspec)
{
	if (e_book_backend_is_opened (backend))
		e_book_backend_notify_online (backend, TRUE);
}

static void
e_book_backend_vcf_finalize (GObject *object)
{
	EBookBackendVCFPrivate *priv;

	priv = E_BOOK_BACKEND_VCF_GET_PRIVATE (object);

	g_mutex_lock (priv->mutex);

	if (priv->flush_timeout_tag)
		g_source_remove (priv->flush_timeout_tag);

	if (priv->dirty)
		save_file (E_BOOK_BACKEND_VCF (object));

	g_hash_table_destroy (priv->contacts);
	g_list_free_full (priv->contact_list, (GDestroyNotify) g_free);

	g_free (priv->filename);

	g_mutex_unlock (priv->mutex);

	g_mutex_free (priv->mutex);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_book_backend_vcf_parent_class)->finalize (object);
}

static void
e_book_backend_vcf_class_init (EBookBackendVCFClass *class)
{
	GObjectClass    *object_class = G_OBJECT_CLASS (class);
	EBookBackendSyncClass *sync_class;
	EBookBackendClass *backend_class;

	g_type_class_add_private (class, sizeof (EBookBackendVCFPrivate));

	sync_class = E_BOOK_BACKEND_SYNC_CLASS (class);
	backend_class = E_BOOK_BACKEND_CLASS (class);

	/* Set the virtual methods. */
	backend_class->start_book_view		= e_book_backend_vcf_start_book_view;
	backend_class->stop_book_view		= e_book_backend_vcf_stop_book_view;

	sync_class->open_sync			= e_book_backend_vcf_open;
	sync_class->get_backend_property_sync	= e_book_backend_vcf_get_backend_property;
	sync_class->create_contacts_sync	= e_book_backend_vcf_create_contacts;
	sync_class->remove_contacts_sync	= e_book_backend_vcf_remove_contacts;
	sync_class->modify_contacts_sync	= e_book_backend_vcf_modify_contacts;
	sync_class->get_contact_sync		= e_book_backend_vcf_get_contact;
	sync_class->get_contact_list_sync	= e_book_backend_vcf_get_contact_list;

	object_class->finalize = e_book_backend_vcf_finalize;

	/* Register our ESource extension. */
	E_TYPE_SOURCE_VCF;
}

static void
e_book_backend_vcf_init (EBookBackendVCF *backend)
{
	backend->priv = E_BOOK_BACKEND_VCF_GET_PRIVATE (backend);
	backend->priv->mutex = g_mutex_new ();

	g_signal_connect (
		backend, "notify::online",
		G_CALLBACK (e_book_backend_vcf_notify_online_cb), NULL);
}
