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

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>

#include "libedataserver/e-data-server-util.h"
#include "libedataserver/e-flag.h"

#include "libebook/e-contact.h"

#include "libedata-book/e-data-book.h"
#include "libedata-book/e-data-book-view.h"
#include "libedata-book/e-book-backend-sexp.h"

#include "e-book-backend-vcf.h"

#define PAS_ID_PREFIX "pas-id-"
#define FILE_FLUSH_TIMEOUT 5000

#define d(x)

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
	   it's doubtful 2^32 id's will be created in a second, so we
	   should be okay. */
	static guint c = 0;
	return g_strdup_printf (PAS_ID_PREFIX "%08lX%08X", time(NULL), c++);
}

static void
insert_contact (EBookBackendVCF *vcf, gchar *vcard)
{
	EContact *contact = e_contact_new_from_vcard (vcard);
	gchar *id;

	id = e_contact_get (contact, E_CONTACT_UID);
	if (id) {
		gchar *vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);

		vcf->priv->contact_list = g_list_prepend (vcf->priv->contact_list, vcard);

		g_hash_table_insert (vcf->priv->contacts,
				     id,
				     vcf->priv->contact_list);
	}
}

static void
load_file (EBookBackendVCF *vcf, gint fd)
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
		g_warning ("Failed to rename %s: %s\n", vcf->priv->filename, g_strerror(errno));
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

	t = time(NULL);
	tm = gmtime (&t);
	if (tm)
		strftime (time_string, 100, "%Y-%m-%dT%H:%M:%SZ", tm);
	e_contact_set (contact, E_CONTACT_REV, time_string);
}

static EContact *
do_create(EBookBackendVCF  *bvcf,
	  const gchar     *vcard_req,
	  gboolean        dirty_the_file)
{
	gchar           *id;
	EContact       *contact;
	gchar           *vcard;
	const gchar     *rev;

	/* at the very least we need the unique_id generation to be
	   protected by the lock, even if the actual vcard parsing
	   isn't. */
	g_mutex_lock (bvcf->priv->mutex);
	id = e_book_backend_vcf_create_unique_id ();

	contact = e_contact_new_from_vcard (vcard_req);
	e_contact_set(contact, E_CONTACT_UID, id);
	g_free (id);

	rev = e_contact_get_const (contact,  E_CONTACT_REV);
	if (!(rev && *rev))
		set_revision (contact);

	vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);

	insert_contact (bvcf, vcard);

	if (dirty_the_file) {
		bvcf->priv->dirty = TRUE;

		if (!bvcf->priv->flush_timeout_tag)
			bvcf->priv->flush_timeout_tag = g_timeout_add (FILE_FLUSH_TIMEOUT,
								       vcf_flush_file, bvcf);
	}

	g_mutex_unlock (bvcf->priv->mutex);

	return contact;
}

static EBookBackendSyncStatus
e_book_backend_vcf_create_contact (EBookBackendSync *backend,
				   EDataBook *book,
				   guint32 opid,
				   const gchar *vcard,
				   EContact **contact)
{
	EBookBackendVCF *bvcf = E_BOOK_BACKEND_VCF (backend);

	*contact = do_create(bvcf, vcard, TRUE);
	if (*contact) {
		return GNOME_Evolution_Addressbook_Success;
	}
	else {
		/* XXX need a different call status for this case, i
                   think */
		return GNOME_Evolution_Addressbook_ContactNotFound;
	}
}

static EBookBackendSyncStatus
e_book_backend_vcf_remove_contacts (EBookBackendSync *backend,
				    EDataBook *book,
				    guint32 opid,
				    GList *id_list,
				    GList **ids)
{
	/* FIXME: make this handle bulk deletes like the file backend does */
	EBookBackendVCF *bvcf = E_BOOK_BACKEND_VCF (backend);
	gchar *id = id_list->data;
	GList *elem;

	g_mutex_lock (bvcf->priv->mutex);
	elem = g_hash_table_lookup (bvcf->priv->contacts, id);
	if (!elem) {
		g_mutex_unlock (bvcf->priv->mutex);
		return GNOME_Evolution_Addressbook_ContactNotFound;
	}

	if (!g_hash_table_remove (bvcf->priv->contacts, id)) {
		g_mutex_unlock (bvcf->priv->mutex);
		return GNOME_Evolution_Addressbook_ContactNotFound;
	}

	g_free (elem->data);
	bvcf->priv->contact_list = g_list_remove_link (bvcf->priv->contact_list, elem);

	bvcf->priv->dirty = TRUE;
	if (!bvcf->priv->flush_timeout_tag)
		bvcf->priv->flush_timeout_tag = g_timeout_add (FILE_FLUSH_TIMEOUT,
							       vcf_flush_file, bvcf);
	g_mutex_unlock (bvcf->priv->mutex);

	*ids = g_list_append (*ids, id);

	return GNOME_Evolution_Addressbook_Success;
}

static EBookBackendSyncStatus
e_book_backend_vcf_modify_contact (EBookBackendSync *backend,
				   EDataBook *book,
				   guint32 opid,
				   const gchar *vcard,
				   EContact **contact)
{
	EBookBackendVCF *bvcf = E_BOOK_BACKEND_VCF (backend);
	GList *elem;
	const gchar *id;

	/* create a new ecard from the request data */
	*contact = e_contact_new_from_vcard (vcard);
	id = e_contact_get_const (*contact, E_CONTACT_UID);

	g_mutex_lock (bvcf->priv->mutex);
	elem = g_hash_table_lookup (bvcf->priv->contacts, id);
	if (!elem) {
		g_mutex_unlock (bvcf->priv->mutex);
		return GNOME_Evolution_Addressbook_ContactNotFound;
	}

	g_free (elem->data);
	elem->data = g_strdup (vcard);
	bvcf->priv->dirty = TRUE;
	if (!bvcf->priv->flush_timeout_tag)
		bvcf->priv->flush_timeout_tag = g_timeout_add (FILE_FLUSH_TIMEOUT,
							       vcf_flush_file, bvcf);
	g_mutex_unlock (bvcf->priv->mutex);

	return GNOME_Evolution_Addressbook_Success;
}

static EBookBackendSyncStatus
e_book_backend_vcf_get_contact (EBookBackendSync *backend,
				EDataBook *book,
				guint32 opid,
				const gchar *id,
				gchar **vcard)
{
	EBookBackendVCF *bvcf = E_BOOK_BACKEND_VCF (backend);
	GList *elem;

	elem = g_hash_table_lookup (bvcf->priv->contacts, id);

	if (elem) {
		*vcard = g_strdup (elem->data);
		return GNOME_Evolution_Addressbook_Success;
	} else {
		*vcard = g_strdup ("");
		return GNOME_Evolution_Addressbook_ContactNotFound;
	}
}

typedef struct {
	EBookBackendVCF      *bvcf;
	gboolean            search_needed;
	EBookBackendSExp *card_sexp;
	GList              *list;
} GetContactListClosure;

static void
foreach_get_contact_compare (gchar *vcard_string, GetContactListClosure *closure)
{
	if ((!closure->search_needed) || e_book_backend_sexp_match_vcard  (closure->card_sexp, vcard_string)) {
		closure->list = g_list_append (closure->list, g_strdup (vcard_string));
	}
}

static EBookBackendSyncStatus
e_book_backend_vcf_get_contact_list (EBookBackendSync *backend,
				     EDataBook *book,
				     guint32 opid,
				     const gchar *query,
				     GList **contacts)
{
	EBookBackendVCF *bvcf = E_BOOK_BACKEND_VCF (backend);
	const gchar *search = query;
	GetContactListClosure closure;

	closure.bvcf = bvcf;
	closure.search_needed = strcmp (search, "(contains \"x-evolution-any-field\" \"\")");
	closure.card_sexp = e_book_backend_sexp_new (search);
	closure.list = NULL;

	g_list_foreach (bvcf->priv->contact_list, (GFunc)foreach_get_contact_compare, &closure);

	g_object_unref (closure.card_sexp);

	*contacts = closure.list;
	return GNOME_Evolution_Addressbook_Success;
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
	d(printf ("destroying search closure\n"));
	e_flag_free (closure->running);
	g_free (closure);
}

static VCFBackendSearchClosure*
init_closure (EDataBookView *book_view, EBookBackendVCF *bvcf)
{
	VCFBackendSearchClosure *closure = g_new (VCFBackendSearchClosure, 1);

	closure->bvcf = bvcf;
	closure->view = book_view;
	closure->thread = NULL;
	closure->running = e_flag_new ();

	g_object_set_data_full (G_OBJECT (book_view), "EBookBackendVCF.BookView::closure",
				closure, (GDestroyNotify)closure_destroy);

	return closure;
}

static VCFBackendSearchClosure*
get_closure (EDataBookView *book_view)
{
	return g_object_get_data (G_OBJECT (book_view), "EBookBackendVCF.BookView::closure");
}

static gpointer
book_view_thread (gpointer data)
{
	EDataBookView *book_view = data;
	VCFBackendSearchClosure *closure = get_closure (book_view);
	const gchar *query;
	GList *l;

	/* ref the book view because it'll be removed and unrefed
	   when/if it's stopped */
	e_data_book_view_ref (book_view);

	query = e_data_book_view_get_card_query (book_view);

	if ( !strcmp (query, "(contains \"x-evolution-any-field\" \"\")"))
		e_data_book_view_notify_status_message (book_view, _("Loading..."));
	else
		e_data_book_view_notify_status_message (book_view, _("Searching..."));

	d(printf ("signalling parent thread\n"));
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
		e_data_book_view_notify_complete (closure->view, GNOME_Evolution_Addressbook_Success);

	/* unref the book view */
	e_data_book_view_unref (book_view);

	d(printf ("finished initial population of book view\n"));

	return NULL;
}

static void
e_book_backend_vcf_start_book_view (EBookBackend  *backend,
				    EDataBookView *book_view)
{
	VCFBackendSearchClosure *closure = init_closure (book_view, E_BOOK_BACKEND_VCF (backend));

	d(printf ("starting book view thread\n"));
	closure->thread = g_thread_create (book_view_thread, book_view, TRUE, NULL);

	e_flag_wait (closure->running);

	/* at this point we know the book view thread is actually running */
	d(printf ("returning from start_book_view\n"));

}

static void
e_book_backend_vcf_stop_book_view (EBookBackend  *backend,
				   EDataBookView *book_view)
{
	VCFBackendSearchClosure *closure = get_closure (book_view);
	gboolean need_join;

	d(printf ("stopping query\n"));
	need_join = e_flag_is_set (closure->running);
	e_flag_clear (closure->running);

	if (need_join)
		g_thread_join (closure->thread);
}

static gchar *
e_book_backend_vcf_extract_path_from_uri (const gchar *uri)
{
	g_assert (g_ascii_strncasecmp (uri, "vcf://", 6) == 0);

	return g_strdup (uri + 6);
}

static EBookBackendSyncStatus
e_book_backend_vcf_authenticate_user (EBookBackendSync *backend,
				      EDataBook *book,
				      guint32 opid,
				      const gchar *user,
				      const gchar *passwd,
				      const gchar *auth_method)
{
	return GNOME_Evolution_Addressbook_Success;
}

static EBookBackendSyncStatus
e_book_backend_vcf_get_required_fields (EBookBackendSync *backend,
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
e_book_backend_vcf_get_supported_fields (EBookBackendSync *backend,
					 EDataBook *book,
					 guint32 opid,
					 GList **fields_out)
{
	GList *fields = NULL;
	gint i;

	/* XXX we need a way to say "we support everything", since the
	   vcf backend does */
	for (i = 0; i < E_CONTACT_FIELD_LAST; i ++)
		fields = g_list_append (fields, (gchar *)e_contact_field_name (i));

	*fields_out = fields;
	return GNOME_Evolution_Addressbook_Success;
}

#ifdef CREATE_DEFAULT_VCARD
# include <libedata-book/ximian-vcard.h>
#endif

static GNOME_Evolution_Addressbook_CallStatus
e_book_backend_vcf_load_source (EBookBackend             *backend,
				ESource                  *source,
				gboolean                  only_if_exists)
{
	EBookBackendVCF *bvcf = E_BOOK_BACKEND_VCF (backend);
	gchar           *dirname;
	gboolean        writable = FALSE;
	gchar          *uri;
	gint fd;

	uri = e_source_get_uri (source);

	dirname = e_book_backend_vcf_extract_path_from_uri (uri);
	bvcf->priv->filename = g_build_filename (dirname, "addressbook.vcf", NULL);

	fd = g_open (bvcf->priv->filename, O_RDWR | O_BINARY, 0);

	bvcf->priv->contacts = g_hash_table_new_full (
		g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) NULL);

	if (fd != -1) {
		writable = TRUE;
	} else {
		fd = g_open (bvcf->priv->filename, O_RDONLY | O_BINARY, 0);

		if (fd == -1 && !only_if_exists) {
			gint rv;

			/* the database didn't exist, so we create the
			   directory then the .vcf file */
			rv = g_mkdir_with_parents (dirname, 0700);
			if (rv == -1 && errno != EEXIST) {
				g_warning ("failed to make directory %s: %s", dirname, g_strerror (errno));
				if (errno == EACCES || errno == EPERM)
					return GNOME_Evolution_Addressbook_PermissionDenied;
				else
					return GNOME_Evolution_Addressbook_OtherError;
			}

			fd = g_open (bvcf->priv->filename, O_CREAT | O_BINARY, 0666);

			if (fd != -1) {
#ifdef CREATE_DEFAULT_VCARD
				EContact *contact;

				contact = do_create(bvcf, XIMIAN_VCARD, FALSE);
				save_file (bvcf);

				/* XXX check errors here */
				g_object_unref (contact);
#endif

				writable = TRUE;
			}
		}
	}

	if (fd == -1) {
		g_warning ("Failed to open addressbook at uri `%s'", uri);
		g_warning ("error == %s", g_strerror(errno));
		g_free (uri);
		return GNOME_Evolution_Addressbook_OtherError;
	}

	load_file (bvcf, fd);

	e_book_backend_set_is_loaded (backend, TRUE);
	e_book_backend_set_is_writable (backend, writable);

	g_free (uri);
	return GNOME_Evolution_Addressbook_Success;
}

static gchar *
e_book_backend_vcf_get_static_capabilities (EBookBackend *backend)
{
	return g_strdup("local,do-initial-query,contact-lists");
}

static GNOME_Evolution_Addressbook_CallStatus
e_book_backend_vcf_cancel_operation (EBookBackend *backend, EDataBook *book)
{
	return GNOME_Evolution_Addressbook_CouldNotCancel;
}

static void
e_book_backend_vcf_set_mode (EBookBackend *backend,
                             GNOME_Evolution_Addressbook_BookMode mode)
{
	if (e_book_backend_is_loaded (backend)) {
		e_book_backend_notify_writable (backend, TRUE);
		e_book_backend_notify_connection_status (backend, TRUE);
	}
}

static gboolean
e_book_backend_vcf_construct (EBookBackendVCF *backend)
{
	g_assert (backend != NULL);
	g_assert (E_IS_BOOK_BACKEND_VCF (backend));

	if (!e_book_backend_construct (E_BOOK_BACKEND (backend)))
		return FALSE;

	return TRUE;
}

/**
 * e_book_backend_vcf_new:
 */
EBookBackend *
e_book_backend_vcf_new (void)
{
	EBookBackendVCF *backend;

	backend = g_object_new (E_TYPE_BOOK_BACKEND_VCF, NULL);

	if (!e_book_backend_vcf_construct (backend)) {
		g_object_unref (backend);

		return NULL;
	}

	return E_BOOK_BACKEND (backend);
}

static void
e_book_backend_vcf_dispose (GObject *object)
{
	EBookBackendVCF *bvcf;

	bvcf = E_BOOK_BACKEND_VCF (object);

	if (bvcf->priv) {

		g_mutex_lock (bvcf->priv->mutex);

		if (bvcf->priv->flush_timeout_tag) {
			g_source_remove (bvcf->priv->flush_timeout_tag);
			bvcf->priv->flush_timeout_tag = 0;
		}

		if (bvcf->priv->dirty)
			save_file (bvcf);

		g_hash_table_destroy (bvcf->priv->contacts);
		g_list_foreach (bvcf->priv->contact_list, (GFunc)g_free, NULL);
		g_list_free (bvcf->priv->contact_list);

		g_free (bvcf->priv->filename);

		g_mutex_unlock (bvcf->priv->mutex);

		g_mutex_free (bvcf->priv->mutex);

		g_free (bvcf->priv);
		bvcf->priv = NULL;
	}

	G_OBJECT_CLASS (e_book_backend_vcf_parent_class)->dispose (object);
}

static void
e_book_backend_vcf_class_init (EBookBackendVCFClass *klass)
{
	GObjectClass    *object_class = G_OBJECT_CLASS (klass);
	EBookBackendSyncClass *sync_class;
	EBookBackendClass *backend_class;

	sync_class = E_BOOK_BACKEND_SYNC_CLASS (klass);
	backend_class = E_BOOK_BACKEND_CLASS (klass);

	/* Set the virtual methods. */
	backend_class->load_source             = e_book_backend_vcf_load_source;
	backend_class->get_static_capabilities = e_book_backend_vcf_get_static_capabilities;
	backend_class->start_book_view         = e_book_backend_vcf_start_book_view;
	backend_class->stop_book_view          = e_book_backend_vcf_stop_book_view;
	backend_class->cancel_operation        = e_book_backend_vcf_cancel_operation;
	backend_class->set_mode                = e_book_backend_vcf_set_mode;
	sync_class->create_contact_sync        = e_book_backend_vcf_create_contact;
	sync_class->remove_contacts_sync       = e_book_backend_vcf_remove_contacts;
	sync_class->modify_contact_sync        = e_book_backend_vcf_modify_contact;
	sync_class->get_contact_sync           = e_book_backend_vcf_get_contact;
	sync_class->get_contact_list_sync      = e_book_backend_vcf_get_contact_list;
	sync_class->authenticate_user_sync     = e_book_backend_vcf_authenticate_user;
	sync_class->get_required_fields_sync   = e_book_backend_vcf_get_required_fields;
	sync_class->get_supported_fields_sync  = e_book_backend_vcf_get_supported_fields;

	object_class->dispose = e_book_backend_vcf_dispose;
}

static void
e_book_backend_vcf_init (EBookBackendVCF *backend)
{
	EBookBackendVCFPrivate *priv;

	priv                 = g_new0 (EBookBackendVCFPrivate, 1);
	priv->mutex = g_mutex_new();

	backend->priv = priv;
}
