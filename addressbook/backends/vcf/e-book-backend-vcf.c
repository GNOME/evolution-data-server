/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Author:
 *   Chris Toshok (toshok@ximian.com)
 *
 * Copyright (C) 2003, Ximian, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>

#include <libgnome/gnome-i18n.h>
#include <libebook/e-contact.h>

#include <libedataserver/e-util.h>
 
#include <libedata-book/e-data-book.h>
#include <libedata-book/e-data-book-view.h>
#include <libedata-book/e-book-backend-sexp.h>
#include "e-book-backend-vcf.h"

#define PAS_ID_PREFIX "pas-id-"
#define FILE_FLUSH_TIMEOUT 5000

#define d(x)

static EBookBackendSyncClass *e_book_backend_vcf_parent_class;
typedef struct _EBookBackendVCFBookView EBookBackendVCFBookView;
typedef struct _EBookBackendVCFSearchContext EBookBackendVCFSearchContext;

struct _EBookBackendVCFPrivate {
	char       *filename;
	GMutex     *mutex;
	GHashTable *contacts;
	gboolean    dirty;
	int         flush_timeout_tag;
};

static char *
e_book_backend_vcf_create_unique_id ()
{
	/* use a 32 counter and the 32 bit timestamp to make an id.
	   it's doubtful 2^32 id's will be created in a second, so we
	   should be okay. */
	static guint c = 0;
	return g_strdup_printf (PAS_ID_PREFIX "%08lX%08X", time(NULL), c++);
}

static void
insert_contact (EBookBackendVCF *vcf, char *vcard)
{
	EContact *contact = e_contact_new_from_vcard (vcard);
	char *id;

	id = e_contact_get (contact, E_CONTACT_UID);
	if (id)
		g_hash_table_insert (vcf->priv->contacts,
				     id,
				     e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30));
}

static void
load_file (EBookBackendVCF *vcf, int fd)
{
	FILE *fp;
	GString *str;
	char buf[1024];

	fp = fdopen (fd, "r");
	if (!fp) {
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

static void
foreach_build_list (char *id, char *vcard_string, GList **list)
{
	*list = g_list_append (*list, vcard_string);
}

static gboolean
save_file (EBookBackendVCF *vcf)
{
	GList *vcards = NULL;
	GList *l;
	char *new_path;
	int fd, rv;

	g_warning ("EBookBackendVCF flushing file to disk");

	g_mutex_lock (vcf->priv->mutex);
	g_hash_table_foreach (vcf->priv->contacts, (GHFunc)foreach_build_list, &vcards);

	new_path = g_strdup_printf ("%s.new", vcf->priv->filename);

	fd = open (new_path, O_CREAT | O_TRUNC | O_WRONLY, 0666);

	for (l = vcards; l; l = l->next) {
		char *vcard_str = l->data;
		int len = strlen (vcard_str);

		rv = write (fd, vcard_str, len);

		if (rv < len) {
			/* XXX */
			g_warning ("write failed.  we need to handle short writes\n");
			close (fd);
			unlink (new_path);
			return FALSE;
		}

		rv = write (fd, "\r\n\r\n", 4);
		if (rv < 4) {
			/* XXX */
			g_warning ("write failed.  we need to handle short writes\n");
			close (fd);
			unlink (new_path);
			return FALSE;
		}
	}

	if (0 > rename (new_path, vcf->priv->filename)) {
		g_warning ("Failed to rename %s: %s\n", vcf->priv->filename, strerror(errno));
		unlink (new_path);
		return FALSE;
	}

	g_list_free (vcards);
	g_free (new_path);

	vcf->priv->dirty = FALSE;

	g_mutex_unlock (vcf->priv->mutex);

	return TRUE;
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

static EContact *
do_create(EBookBackendVCF  *bvcf,
	  const char     *vcard_req,
	  gboolean        dirty_the_file)
{
	char           *id;
	EContact       *contact;
	char           *vcard;

	/* at the very least we need the unique_id generation to be
	   protected by the lock, even if the actual vcard parsing
	   isn't. */
	g_mutex_lock (bvcf->priv->mutex);
	id = e_book_backend_vcf_create_unique_id ();

	contact = e_contact_new_from_vcard (vcard_req);
	e_contact_set(contact, E_CONTACT_UID, id);
	vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);

	g_hash_table_insert (bvcf->priv->contacts, id, vcard);

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
				   const char *vcard,
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
	char *id = id_list->data;
	gboolean success;

	g_mutex_lock (bvcf->priv->mutex);
	success = g_hash_table_remove (bvcf->priv->contacts, id);

	if (!success) {
		g_mutex_unlock (bvcf->priv->mutex);
		return GNOME_Evolution_Addressbook_ContactNotFound;
	}
	else {
		bvcf->priv->dirty = TRUE;
		if (!bvcf->priv->flush_timeout_tag)
			bvcf->priv->flush_timeout_tag = g_timeout_add (FILE_FLUSH_TIMEOUT,
								       vcf_flush_file, bvcf);
		g_mutex_unlock (bvcf->priv->mutex);

		*ids = g_list_append (*ids, id);

		return GNOME_Evolution_Addressbook_Success;
	}
}

static EBookBackendSyncStatus
e_book_backend_vcf_modify_contact (EBookBackendSync *backend,
				   EDataBook *book,
				   guint32 opid,
				   const char *vcard,
				   EContact **contact)
{
	EBookBackendVCF *bvcf = E_BOOK_BACKEND_VCF (backend);
	char *old_id, *old_vcard_string;
	const char *id;
	gboolean success;

	/* create a new ecard from the request data */
	*contact = e_contact_new_from_vcard (vcard);
	id = e_contact_get_const (*contact, E_CONTACT_UID);

	g_mutex_lock (bvcf->priv->mutex);
	success = g_hash_table_lookup_extended (bvcf->priv->contacts, id, (gpointer)&old_id, (gpointer)&old_vcard_string);

	if (!success) {
		g_mutex_unlock (bvcf->priv->mutex);
		return GNOME_Evolution_Addressbook_ContactNotFound;
	}
	else {
		g_hash_table_insert (bvcf->priv->contacts, old_id, g_strdup (vcard));
		bvcf->priv->dirty = TRUE;
		if (!bvcf->priv->flush_timeout_tag)
			bvcf->priv->flush_timeout_tag = g_timeout_add (FILE_FLUSH_TIMEOUT,
								       vcf_flush_file, bvcf);
		g_mutex_unlock (bvcf->priv->mutex);

		g_free (old_vcard_string);

		return GNOME_Evolution_Addressbook_Success;
	}
}

static EBookBackendSyncStatus
e_book_backend_vcf_get_contact (EBookBackendSync *backend,
				EDataBook *book,
				guint32 opid,
				const char *id,
				char **vcard)
{
	EBookBackendVCF *bvcf = E_BOOK_BACKEND_VCF (backend);
	char *v;

	v = g_hash_table_lookup (bvcf->priv->contacts, id);

	if (v) {
		*vcard = g_strdup (v);
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
foreach_get_contact_compare (char *id, char *vcard_string, GetContactListClosure *closure)
{
	if ((!closure->search_needed) || e_book_backend_sexp_match_vcard  (closure->card_sexp, vcard_string)) {
		closure->list = g_list_append (closure->list, g_strdup (vcard_string));
	}
}

static EBookBackendSyncStatus
e_book_backend_vcf_get_contact_list (EBookBackendSync *backend,
				     EDataBook *book,
				     guint32 opid,
				     const char *query,
				     GList **contacts)
{
	EBookBackendVCF *bvcf = E_BOOK_BACKEND_VCF (backend);
	const char *search = query;
	GetContactListClosure closure;

	closure.bvcf = bvcf;
	closure.search_needed = strcmp (search, "(contains \"x-evolution-any-field\" \"\")");
	closure.card_sexp = e_book_backend_sexp_new (search);
	closure.list = NULL;

	g_hash_table_foreach (bvcf->priv->contacts, (GHFunc)foreach_get_contact_compare, &closure);

	g_object_unref (closure.card_sexp);

	*contacts = closure.list;
	return GNOME_Evolution_Addressbook_Success;
}

typedef struct {
	EBookBackendVCF *bvcf;
	EDataBookView *view;
	GMutex *mutex;
	GCond *cond;
	GThread *thread;
	gboolean stopped;
} VCFBackendSearchClosure;

static void
closure_destroy (VCFBackendSearchClosure *closure)
{
	d(printf ("destroying search closure\n"));
	g_mutex_free (closure->mutex);
	g_cond_free (closure->cond);
	g_free (closure);
}

static VCFBackendSearchClosure*
init_closure (EDataBookView *book_view, EBookBackendVCF *bvcf)
{
	VCFBackendSearchClosure *closure = g_new (VCFBackendSearchClosure, 1);

	closure->bvcf = bvcf;
	closure->view = book_view;
	closure->mutex = g_mutex_new ();
	closure->cond = g_cond_new ();
	closure->thread = NULL;
	closure->stopped = FALSE;

	g_object_set_data_full (G_OBJECT (book_view), "EBookBackendVCF.BookView::closure",
				closure, (GDestroyNotify)closure_destroy);

	return closure;
}

static VCFBackendSearchClosure*
get_closure (EDataBookView *book_view)
{
	return g_object_get_data (G_OBJECT (book_view), "EBookBackendVCF.BookView::closure");
}

static void
foreach_search_compare (char *id, char *vcard_string, VCFBackendSearchClosure *closure)
{
	EContact *contact;

	/* we really need to stop the entire loop when stopped ==
	   TRUE, but we can't loop over the hash table that way..
	   lame.  so we just return here. */
	if (closure->stopped)
		return;

	contact = e_contact_new_from_vcard (vcard_string);
	e_data_book_view_notify_update (closure->view, contact);
	g_object_unref (contact);
}

static gpointer
book_view_thread (gpointer data)
{
	EDataBookView *book_view = data;
	VCFBackendSearchClosure *closure = get_closure (book_view);
	const char *query;

	/* ref the book view because it'll be removed and unrefed
	   when/if it's stopped */
	bonobo_object_ref (book_view);

	query = e_data_book_view_get_card_query (book_view);

	if ( ! strcmp (query, "(contains \"x-evolution-any-field\" \"\")"))
		e_data_book_view_notify_status_message (book_view, _("Loading..."));
	else
		e_data_book_view_notify_status_message (book_view, _("Searching..."));

	d(printf ("signalling parent thread\n"));
	g_mutex_lock (closure->mutex);
	g_cond_signal (closure->cond);
	g_mutex_unlock (closure->mutex);

	g_hash_table_foreach (closure->bvcf->priv->contacts,
			      (GHFunc)foreach_search_compare,
			      closure);

	if (!closure->stopped)
		e_data_book_view_notify_complete (closure->view, GNOME_Evolution_Addressbook_Success);

	/* unref the book view */
	bonobo_object_unref (book_view);

	d(printf ("finished initial population of book view\n"));

	return NULL;
}


static void
e_book_backend_vcf_start_book_view (EBookBackend  *backend,
				    EDataBookView *book_view)
{
	VCFBackendSearchClosure *closure = init_closure (book_view, E_BOOK_BACKEND_VCF (backend));

	g_mutex_lock (closure->mutex);

	d(printf ("starting book view thread\n"));
	closure->thread = g_thread_create (book_view_thread, book_view, TRUE, NULL);

	g_cond_wait (closure->cond, closure->mutex);

	/* at this point we know the book view thread is actually running */
	g_mutex_unlock (closure->mutex);
	d(printf ("returning from start_book_view\n"));

}

static void
e_book_backend_vcf_stop_book_view (EBookBackend  *backend,
				   EDataBookView *book_view)
{
	VCFBackendSearchClosure *closure = get_closure (book_view);
	gboolean need_join = FALSE;

	d(printf ("stopping query\n"));
	g_mutex_lock (closure->mutex);
	if (!closure->stopped)
		need_join = TRUE;
	closure->stopped = TRUE;
	g_mutex_unlock (closure->mutex);

	if (need_join)
		g_thread_join (closure->thread);
}

static char *
e_book_backend_vcf_extract_path_from_uri (const char *uri)
{
	g_assert (strncasecmp (uri, "vcf://", 6) == 0);

	return g_strdup (uri + 6);
}

static EBookBackendSyncStatus
e_book_backend_vcf_authenticate_user (EBookBackendSync *backend,
				      EDataBook *book,
				      guint32 opid,
				      const char *user,
				      const char *passwd,
				      const char *auth_method)
{
	return GNOME_Evolution_Addressbook_Success;
}

static EBookBackendSyncStatus
e_book_backend_vcf_get_supported_fields (EBookBackendSync *backend,
					 EDataBook *book,
					 guint32 opid,
					 GList **fields_out)
{
	GList *fields = NULL;
	int i;

	/* XXX we need a way to say "we support everything", since the
	   vcf backend does */
	for (i = 0; i < E_CONTACT_FIELD_LAST; i ++)
		fields = g_list_append (fields, (char*)e_contact_field_name (i));		

	*fields_out = fields;
	return GNOME_Evolution_Addressbook_Success;
}

#include <libedata-book/ximian-vcard.h>

static GNOME_Evolution_Addressbook_CallStatus
e_book_backend_vcf_load_source (EBookBackend             *backend,
				ESource                  *source,
				gboolean                  only_if_exists)
{
	EBookBackendVCF *bvcf = E_BOOK_BACKEND_VCF (backend);
	char           *dirname;
	gboolean        writable = FALSE;
	gchar          *uri;
	int fd;

	uri = e_source_get_uri (source);

	dirname = e_book_backend_vcf_extract_path_from_uri (uri);
	bvcf->priv->filename = g_build_filename (dirname, "addressbook.vcf", NULL);

	fd = open (bvcf->priv->filename, O_RDWR);

	bvcf->priv->contacts = g_hash_table_new_full (g_str_hash, g_str_equal,
						      g_free, g_free);

	if (fd != -1) {
		writable = TRUE;
	} else {
		fd = open (bvcf->priv->filename, O_RDONLY);

		if (fd == -1 && !only_if_exists) {
			int rv;

			/* the database didn't exist, so we create the
			   directory then the .vcf file */
			rv = e_util_mkdir_hier (dirname, 0777);
			if (rv == -1 && errno != EEXIST) {
				g_warning ("failed to make directory %s: %s", dirname, strerror (errno));
				if (errno == EACCES || errno == EPERM)
					return GNOME_Evolution_Addressbook_PermissionDenied;
				else
					return GNOME_Evolution_Addressbook_OtherError;
			}

			fd = open (bvcf->priv->filename, O_CREAT, 0666);

			if (fd != -1) {
				EContact *contact;

				contact = do_create(bvcf, XIMIAN_VCARD, FALSE);
				save_file (bvcf);

				/* XXX check errors here */
				g_object_unref (contact);

				writable = TRUE;
			}
		}
	}

	if (fd == -1) {
		g_warning ("Failed to open addressbook at uri `%s'", uri);
		g_warning ("error == %s", strerror(errno));
		g_free (uri);
		return GNOME_Evolution_Addressbook_OtherError;
	}

	load_file (bvcf, fd);

	e_book_backend_set_is_loaded (backend, TRUE);
	e_book_backend_set_is_writable (backend, writable);

	g_free (uri);
	return GNOME_Evolution_Addressbook_Success;
}

static char *
e_book_backend_vcf_get_static_capabilities (EBookBackend *backend)
{
	return g_strdup("local,do-initial-query");
}

static GNOME_Evolution_Addressbook_CallStatus
e_book_backend_vcf_cancel_operation (EBookBackend *backend, EDataBook *book)
{
	return GNOME_Evolution_Addressbook_CouldNotCancel;
}

static gboolean
e_book_backend_vcf_construct (EBookBackendVCF *backend)
{
	g_assert (backend != NULL);
	g_assert (E_IS_BOOK_BACKEND_VCF (backend));

	if (! e_book_backend_construct (E_BOOK_BACKEND (backend)))
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

	if (! e_book_backend_vcf_construct (backend)) {
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

	e_book_backend_vcf_parent_class = g_type_class_peek_parent (klass);

	sync_class = E_BOOK_BACKEND_SYNC_CLASS (klass);
	backend_class = E_BOOK_BACKEND_CLASS (klass);

	/* Set the virtual methods. */
	backend_class->load_source             = e_book_backend_vcf_load_source;
	backend_class->get_static_capabilities = e_book_backend_vcf_get_static_capabilities;
	backend_class->start_book_view         = e_book_backend_vcf_start_book_view;
	backend_class->stop_book_view          = e_book_backend_vcf_stop_book_view;
	backend_class->cancel_operation        = e_book_backend_vcf_cancel_operation;

	sync_class->create_contact_sync        = e_book_backend_vcf_create_contact;
	sync_class->remove_contacts_sync       = e_book_backend_vcf_remove_contacts;
	sync_class->modify_contact_sync        = e_book_backend_vcf_modify_contact;
	sync_class->get_contact_sync           = e_book_backend_vcf_get_contact;
	sync_class->get_contact_list_sync      = e_book_backend_vcf_get_contact_list;
	sync_class->authenticate_user_sync     = e_book_backend_vcf_authenticate_user;
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

/**
 * e_book_backend_vcf_get_type:
 */
GType
e_book_backend_vcf_get_type (void)
{
	static GType type = 0;

	if (! type) {
		GTypeInfo info = {
			sizeof (EBookBackendVCFClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc)  e_book_backend_vcf_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (EBookBackendVCF),
			0,    /* n_preallocs */
			(GInstanceInitFunc) e_book_backend_vcf_init
		};

		type = g_type_register_static (E_TYPE_BOOK_BACKEND_SYNC, "EBookBackendVCF", &info, 0);
	}

	return type;
}
