/*
 * SPDX-FileCopyrightText: (C) 2023 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-data-server-config.h"

#include <glib.h>

#include <libedataserver/libedataserver.h>

#include "e-book-backend.h"
#include "e-data-book-view.h"

#include "e-data-book-view-watcher-memory.h"

/**
 * SECTION: e-data-book-view-watcher-memory
 * @include: libedata-book/libedata-book.h
 * @short_description: Watch #EDataBookView changes with contacts in memory
 *
 * This is the default implementation of "manual query" views for an #EBookBackend.
 * It listens for the changes in the provided #EDataBookView and keeps in memory
 * information about the indices of the book view and the contacts.
 *
 * See %E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY for what it means "manual query" view.
 **/

struct _EDataBookViewWatcherMemoryPrivate {
	GWeakRef backend_weakref; /* EBookBackend * */
	GWeakRef view_weakref; /* EDataBookView * */
	GMutex property_lock;

	gulong signal_objects_added_id;
	gulong signal_objects_modified_id;
	gulong signal_objects_removed_id;

	guint64 sort_fields_stamp;

	EBookClientViewSortFields *sort_fields;
	ECollator *collator;
	GPtrArray *contacts; /* ContactData */
};

G_DEFINE_TYPE_WITH_PRIVATE (EDataBookViewWatcherMemory, e_data_book_view_watcher_memory, E_TYPE_BOOK_INDICES_UPDATER)

typedef struct _ContactData {
	EContact *contact;
	gchar **sort_keys; /* collation keys */
	guint64 stamp; /* sort keys are valid if the stamp matches view_watcher::stamp */
	gint indices_index;
} ContactData;

static ContactData *
contact_data_new (EContact *contact, /* (transfer full) */
		  guint64 stamp)
{
	ContactData *cd;

	cd = g_new0 (ContactData, 1);
	cd->contact = contact;
	cd->stamp = stamp - 1; /* the sort_keys is invalid */

	return cd;
}

static void
contact_data_free (gpointer ptr)
{
	ContactData *cd = ptr;

	if (cd) {
		g_clear_object (&cd->contact);
		g_strfreev (cd->sort_keys);
		g_free (cd);
	}
}

static void
contact_data_update_sort_keys (ContactData *cd,
			       const EBookClientViewSortFields *sort_fields,
			       guint64 sort_fields_stamp,
			       ECollator *collator)
{
	guint ii, n_fields = 0;
	gboolean first_sort_value = TRUE;

	for (ii = 0; sort_fields && sort_fields[ii].field != E_CONTACT_FIELD_LAST; ii++) {
		if (e_contact_field_type (sort_fields[ii].field) == G_TYPE_STRING) {
			n_fields++;
		}
	}

	g_strfreev (cd->sort_keys);

	cd->stamp = sort_fields_stamp;
	cd->indices_index = 0;
	cd->sort_keys = g_new0 (gchar *, n_fields + 1);

	for (ii = 0; sort_fields && sort_fields[ii].field != E_CONTACT_FIELD_LAST; ii++) {
		if (e_contact_field_type (sort_fields[ii].field) == G_TYPE_STRING) {
			gchar *value = e_contact_get (cd->contact, sort_fields[ii].field);

			cd->sort_keys[ii] = value ? e_collator_generate_key (collator, value, NULL) : NULL;
			if (first_sort_value) {
				first_sort_value = FALSE;
				cd->indices_index = e_collator_get_index (collator, value ? value : "");
			}

			g_free (value);
		}

		if (!cd->sort_keys[ii])
			cd->sort_keys[ii] = g_strdup ("");
	}
}

static gint
contact_data_get_indices_index (ContactData *cd,
				const EBookClientViewSortFields *sort_fields,
				guint64 sort_fields_stamp,
				ECollator *collator)
{
	if (cd->stamp != sort_fields_stamp)
		contact_data_update_sort_keys (cd, sort_fields, sort_fields_stamp, collator);

	return cd->indices_index;
}

static gint
data_book_view_watcher_memory_compare (EDataBookViewWatcherMemory *self,
				       ContactData *cd1,
				       ContactData *cd2)
{
	gint res = 0;
	guint ii;

	if (cd1->stamp != self->priv->sort_fields_stamp)
		contact_data_update_sort_keys (cd1, self->priv->sort_fields, self->priv->sort_fields_stamp, self->priv->collator);

	if (cd2->stamp != self->priv->sort_fields_stamp)
		contact_data_update_sort_keys (cd2, self->priv->sort_fields, self->priv->sort_fields_stamp, self->priv->collator);

	for (ii = 0; cd1->sort_keys && cd1->sort_keys[ii] && cd2->sort_keys && cd2->sort_keys[ii] && !res; ii++) {
		res = g_strcmp0 (cd1->sort_keys[ii], cd2->sort_keys[ii]);
		if (res && self->priv->sort_fields[ii].sort_type == E_BOOK_CURSOR_SORT_DESCENDING)
			res = res * (-1);
	}

	if (!res) {
		res = g_strcmp0 (e_contact_get_const (cd1->contact, E_CONTACT_UID),
				 e_contact_get_const (cd2->contact, E_CONTACT_UID));

		if (!res) {
			if (cd1->contact < cd2->contact)
				res = -1;
			else if (cd1->contact > cd2->contact)
				res = 1;
		}

		if (self->priv->sort_fields && self->priv->sort_fields[0].sort_type == E_BOOK_CURSOR_SORT_DESCENDING)
			res = res * (-1);
	}

	return res;
}

static gint
data_book_view_watcher_memory_compare_cb (gconstpointer aa,
					  gconstpointer bb,
					  gpointer user_data)
{
	EDataBookViewWatcherMemory *self = user_data;
	ContactData *cd1 = *((ContactData **) aa);
	ContactData *cd2 = *((ContactData **) bb);

	return data_book_view_watcher_memory_compare (self, cd1, cd2);
}

static EBookBackend *
data_book_view_watcher_memory_ref_backend (EDataBookViewWatcherMemory *self)
{
	return g_weak_ref_get (&self->priv->backend_weakref);
}

static EDataBookView *
data_book_view_watcher_memory_ref_view (EDataBookViewWatcherMemory *self)
{
	return g_weak_ref_get (&self->priv->view_weakref);
}

static void
data_book_view_watcher_memory_update_n_total (EDataBookViewWatcherMemory *self,
					      guint n_total)
{
	EBookBackend *backend;
	EDataBookView *view;

	backend = data_book_view_watcher_memory_ref_backend (self);
	view = data_book_view_watcher_memory_ref_view (self);

	if (backend && view)
		e_book_backend_set_view_n_total (backend, e_data_book_view_get_id (view), n_total);

	g_clear_object (&backend);
	g_clear_object (&view);
}

static void
data_book_view_watcher_memory_update_indices (EDataBookViewWatcherMemory *self)
{
	EBookIndicesUpdater *indices_updater = E_BOOK_INDICES_UPDATER (self);
	EBookBackend *backend;
	EDataBookView *view;

	backend = data_book_view_watcher_memory_ref_backend (self);
	view = data_book_view_watcher_memory_ref_view (self);

	g_mutex_lock (&self->priv->property_lock);

	e_book_indices_updater_take_indices (indices_updater, NULL);

	if (self->priv->collator) {
		const gchar * const *labels;
		gint n_labels = 0;

		labels = e_collator_get_index_labels (self->priv->collator, &n_labels, NULL, NULL, NULL);

		if (labels && n_labels > 0) {
			EBookIndices *indices;
			guint ii;

			indices = g_new0 (EBookIndices, n_labels + 1);

			for (ii = 0; ii < n_labels; ii++) {
				indices[ii].chr = g_strdup (labels[ii]);
				indices[ii].index = G_MAXUINT;
			}

			e_book_indices_set_ascending_sort (indices_updater, !self->priv->sort_fields ||
				self->priv->sort_fields[0].sort_type == E_BOOK_CURSOR_SORT_ASCENDING);
			e_book_indices_updater_take_indices (indices_updater, indices);

			for (ii = 0; ii < self->priv->contacts->len; ii++) {
				ContactData *cd = g_ptr_array_index (self->priv->contacts, ii);
				gint indices_index = contact_data_get_indices_index (cd, self->priv->sort_fields, self->priv->sort_fields_stamp, self->priv->collator);

				e_book_indices_updater_add (indices_updater, e_contact_get_const (cd->contact, E_CONTACT_UID), indices_index);
			}
		}
	}

	if (backend && view)
		e_book_backend_set_view_indices (backend, e_data_book_view_get_id (view), e_book_indices_updater_get_indices (indices_updater));

	g_mutex_unlock (&self->priv->property_lock);

	g_clear_object (&backend);
	g_clear_object (&view);
}

static void
data_book_view_watcher_memory_notify_content_changed (EDataBookViewWatcherMemory *self)
{
	EDataBookView *view;

	view = data_book_view_watcher_memory_ref_view (self);

	if (view) {
		e_data_book_view_notify_content_changed (view);
		g_object_unref (view);
	}
}

static void
data_book_view_watcher_memory_notify_indices_changed_locked (EDataBookViewWatcherMemory *self)
{
	EBookBackend *backend;
	EDataBookView *view;

	backend = data_book_view_watcher_memory_ref_backend (self);
	view = data_book_view_watcher_memory_ref_view (self);

	if (backend && view)
		e_book_backend_set_view_indices (backend, e_data_book_view_get_id (view), e_book_indices_updater_get_indices (E_BOOK_INDICES_UPDATER (self)));

	g_clear_object (&backend);
	g_clear_object (&view);
}

static gboolean
data_book_view_watcher_memory_place_contact_data_locked (EDataBookViewWatcherMemory *self,
							 ContactData *cd)
{
	gint indices_index = contact_data_get_indices_index (cd, self->priv->sort_fields, self->priv->sort_fields_stamp, self->priv->collator);
	guint new_index = 0;

	if (!self->priv->contacts->len) {
		new_index = 0;
		g_ptr_array_add (self->priv->contacts, cd);
	} else if (self->priv->contacts->len == 1) {
		gint cmp_value;

		cmp_value = data_book_view_watcher_memory_compare (self, cd, g_ptr_array_index (self->priv->contacts, 0));
		if (cmp_value < 0) {
			new_index = 0;
			g_ptr_array_insert (self->priv->contacts, 0, cd);
		} else {
			new_index = 1;
			g_ptr_array_add (self->priv->contacts, cd);
		}
	} else {
		gint cmp_value;

		cmp_value = data_book_view_watcher_memory_compare (self, cd, g_ptr_array_index (self->priv->contacts, 0));
		if (cmp_value < 0) {
			new_index = 0;
			g_ptr_array_insert (self->priv->contacts, 0, cd);
		} else {
			cmp_value = data_book_view_watcher_memory_compare (self, cd, g_ptr_array_index (self->priv->contacts, self->priv->contacts->len - 1));
			if (cmp_value > 0) {
				new_index = self->priv->contacts->len;
				g_ptr_array_add (self->priv->contacts, cd);
			} else {
				guint min_index = 0, max_index = self->priv->contacts->len - 1;

				while (min_index + 1 < max_index) {
					guint mid_index = min_index + (max_index - min_index + 1) / 2;

					cmp_value = data_book_view_watcher_memory_compare (self, cd, g_ptr_array_index (self->priv->contacts, mid_index));
					if (cmp_value < 0)
						max_index = mid_index;
					else
						min_index = mid_index;
				}

				new_index = min_index + 1;
				g_ptr_array_insert (self->priv->contacts, new_index, cd);
			}
		}
	}

	return e_book_indices_updater_add (E_BOOK_INDICES_UPDATER (self), e_contact_get_const (cd->contact, E_CONTACT_UID), indices_index);
}

static guint
data_book_view_watcher_memory_find_contact_index_locked (EDataBookViewWatcherMemory *self,
							 const gchar *uid)
{
	guint ii;

	if (!uid)
		return G_MAXUINT;

	for (ii = 0; ii < self->priv->contacts->len; ii++) {
		ContactData *cd = g_ptr_array_index (self->priv->contacts, ii);

		if (g_strcmp0 (uid, e_contact_get_const (cd->contact, E_CONTACT_UID)) == 0)
			break;
	}

	return ii;
}

static gboolean
data_book_view_watcher_memory_remove_index_locked (EDataBookViewWatcherMemory *self,
						   guint index,
						   gboolean delete_data)
{
	ContactData *cd;
	gboolean indices_changed;

	g_return_val_if_fail (index < self->priv->contacts->len, FALSE);

	cd = g_ptr_array_index (self->priv->contacts, index);

	indices_changed = e_book_indices_updater_remove (E_BOOK_INDICES_UPDATER (self), e_contact_get_const (cd->contact, E_CONTACT_UID));

	if (!delete_data)
		self->priv->contacts->pdata[index] = NULL;

	g_ptr_array_remove_index (self->priv->contacts, index);

	return indices_changed;
}

static void
e_data_book_view_watcher_memory_objects_added_cb (EDataBookView *view,
						  const gchar * const *vcard_uids,
						  gpointer user_data)
{
	EDataBookViewWatcherMemory *self = user_data;
	gboolean indices_changed = FALSE;
	guint ii, n_total;

	g_return_if_fail (E_IS_DATA_BOOK_VIEW_WATCHER_MEMORY (self));
	g_return_if_fail (vcard_uids != NULL);

	g_mutex_lock (&self->priv->property_lock);

	for (ii = 0; vcard_uids[ii] && vcard_uids[ii + 1]; ii += 2) {
		const gchar *vcard, *uid;
		EContact *contact;

		vcard = vcard_uids[ii];
		uid = vcard_uids[ii + 1];

		if (!uid) {
			g_warn_if_reached ();
			break;
		}

		contact = e_contact_new_from_vcard_with_uid (vcard, uid);
		if (contact) {
			ContactData *cd;

			cd = contact_data_new (contact, self->priv->sort_fields_stamp);
			indices_changed = data_book_view_watcher_memory_place_contact_data_locked (self, cd) || indices_changed;
		}
	}

	n_total = self->priv->contacts->len;

	if (indices_changed)
		data_book_view_watcher_memory_notify_indices_changed_locked (self);

	g_mutex_unlock (&self->priv->property_lock);

	data_book_view_watcher_memory_update_n_total (self, n_total);
	data_book_view_watcher_memory_notify_content_changed (self);
}

static void
e_data_book_view_watcher_memory_objects_modified_cb (EDataBookView *view,
						     const gchar * const *vcard_uids,
						     gpointer user_data)
{
	EDataBookViewWatcherMemory *self = user_data;
	gboolean content_changed = FALSE;
	gboolean indices_changed = FALSE;
	guint ii;

	g_return_if_fail (E_IS_DATA_BOOK_VIEW_WATCHER_MEMORY (self));
	g_return_if_fail (vcard_uids != NULL);

	g_mutex_lock (&self->priv->property_lock);

	for (ii = 0; vcard_uids[ii] && vcard_uids[ii + 1]; ii += 2) {
		const gchar *vcard, *uid;
		EContact *contact;

		vcard = vcard_uids[ii];
		uid = vcard_uids[ii + 1];

		if (!uid) {
			g_warn_if_reached ();
			break;
		}

		contact = e_contact_new_from_vcard_with_uid (vcard, uid);
		if (contact) {
			guint old_index;

			old_index = data_book_view_watcher_memory_find_contact_index_locked (self, e_contact_get_const (contact, E_CONTACT_UID));

			if (old_index < self->priv->contacts->len) {
				ContactData *old_cd = g_ptr_array_index (self->priv->contacts, old_index);
				EContact *old_contact;
				gchar **old_sort_keys;
				guint jj;

				/* maybe the sort order did not change, but the contact did change */
				content_changed = TRUE;

				if (!old_cd->sort_keys || old_cd->stamp != self->priv->sort_fields_stamp)
					contact_data_update_sort_keys (old_cd, self->priv->sort_fields, self->priv->sort_fields_stamp, self->priv->collator);

				old_contact = old_cd->contact;
				old_cd->contact = contact;
				old_sort_keys = old_cd->sort_keys;
				old_cd->sort_keys = NULL;

				contact_data_update_sort_keys (old_cd, self->priv->sort_fields, self->priv->sort_fields_stamp, self->priv->collator);

				old_cd->stamp = self->priv->sort_fields_stamp;

				for (jj = 0; old_cd->sort_keys[jj] && old_sort_keys[jj]; jj++) {
					if (g_strcmp0 (old_cd->sort_keys[jj], old_sort_keys[jj]) != 0)
						break;
				}

				/* it's non-NULL when the sort data changed */
				if (old_cd->sort_keys[jj] || old_sort_keys[jj]) {
					old_cd->contact = old_contact;
					indices_changed = data_book_view_watcher_memory_remove_index_locked (self, old_index, FALSE) || indices_changed;
					old_cd->contact = contact;
					indices_changed = data_book_view_watcher_memory_place_contact_data_locked (self, old_cd) || indices_changed;
				}

				g_clear_object (&old_contact);
				g_strfreev (old_sort_keys);
			}
		}
	}

	if (indices_changed)
		data_book_view_watcher_memory_notify_indices_changed_locked (self);

	g_mutex_unlock (&self->priv->property_lock);

	if (content_changed)
		data_book_view_watcher_memory_notify_content_changed (self);
}

static void
e_data_book_view_watcher_memory_objects_removed_cb (EDataBookView *view,
						    const gchar * const *uids,
						    gpointer user_data)
{
	EDataBookViewWatcherMemory *self = user_data;
	gboolean content_changed = FALSE;
	gboolean indices_changed = FALSE;
	guint ii, n_total;

	g_return_if_fail (E_IS_DATA_BOOK_VIEW_WATCHER_MEMORY (self));
	g_return_if_fail (uids != NULL);

	g_mutex_lock (&self->priv->property_lock);

	for (ii = 0; uids[ii]; ii++) {
		guint index;

		index = data_book_view_watcher_memory_find_contact_index_locked (self, uids[ii]);
		if (index < self->priv->contacts->len) {
			content_changed = TRUE;
			indices_changed = data_book_view_watcher_memory_remove_index_locked (self, index, TRUE) || indices_changed;
		}
	}

	n_total = self->priv->contacts->len;

	if (indices_changed)
		data_book_view_watcher_memory_notify_indices_changed_locked (self);

	g_mutex_unlock (&self->priv->property_lock);

	if (content_changed) {
		data_book_view_watcher_memory_update_n_total (self, n_total);
		data_book_view_watcher_memory_notify_content_changed (self);
	}
}

static void
data_book_view_watcher_memory_dispose (GObject *object)
{
	EDataBookViewWatcherMemory *self = E_DATA_BOOK_VIEW_WATCHER_MEMORY (object);
	EDataBookView *view;

	view = data_book_view_watcher_memory_ref_view (self);
	if (view) {
		if (self->priv->signal_objects_added_id) {
			g_signal_handler_disconnect (view, self->priv->signal_objects_added_id);
			self->priv->signal_objects_added_id = 0;
		}

		if (self->priv->signal_objects_modified_id) {
			g_signal_handler_disconnect (view, self->priv->signal_objects_modified_id);
			self->priv->signal_objects_modified_id = 0;
		}

		if (self->priv->signal_objects_removed_id) {
			g_signal_handler_disconnect (view, self->priv->signal_objects_removed_id);
			self->priv->signal_objects_removed_id = 0;
		}

		g_object_unref (view);
	}

	g_weak_ref_set (&self->priv->backend_weakref, NULL);
	g_weak_ref_set (&self->priv->view_weakref, NULL);
	g_clear_pointer (&self->priv->sort_fields, e_book_client_view_sort_fields_free);
	g_clear_pointer (&self->priv->collator, e_collator_unref);
	g_clear_pointer (&self->priv->contacts, g_ptr_array_unref);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_data_book_view_watcher_memory_parent_class)->dispose (object);
}

static void
data_book_view_watcher_memory_finalize (GObject *object)
{
	EDataBookViewWatcherMemory *self = E_DATA_BOOK_VIEW_WATCHER_MEMORY (object);

	g_weak_ref_clear (&self->priv->backend_weakref);
	g_weak_ref_clear (&self->priv->view_weakref);
	g_mutex_clear (&self->priv->property_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_data_book_view_watcher_memory_parent_class)->finalize (object);
}

static void
e_data_book_view_watcher_memory_class_init (EDataBookViewWatcherMemoryClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->dispose = data_book_view_watcher_memory_dispose;
	object_class->finalize = data_book_view_watcher_memory_finalize;
}

static void
e_data_book_view_watcher_memory_init (EDataBookViewWatcherMemory *self)
{
	self->priv = e_data_book_view_watcher_memory_get_instance_private (self);
	self->priv->sort_fields_stamp = 1;
	self->priv->contacts = g_ptr_array_new_with_free_func (contact_data_free);

	g_weak_ref_init (&self->priv->backend_weakref, NULL);
	g_weak_ref_init (&self->priv->view_weakref, NULL);
	g_mutex_init (&self->priv->property_lock);
}

/**
 * e_data_book_view_watcher_memory_new:
 * @backend: an #EBookBackend
 * @view: an #EDataBookView
 *
 * Creates a new #EDataBookViewWatcherMemory, which will watch the @view
 * and will provide the information about indices and total contacts
 * to the @backend. The locale is taken from the @backend during
 * the creation process too.
 *
 * Returns: (transfer full): a new #EDataBookViewWatcherMemory
 *
 * Since: 3.50
 **/
GObject *
e_data_book_view_watcher_memory_new (EBookBackend *backend,
				     EDataBookView *view)
{
	EDataBookViewWatcherMemory *self;
	gchar *locale;

	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);
	g_return_val_if_fail (E_IS_DATA_BOOK_VIEW (view), NULL);

	self = g_object_new (E_TYPE_DATA_BOOK_VIEW_WATCHER_MEMORY, NULL);

	g_weak_ref_set (&self->priv->backend_weakref, backend);
	g_weak_ref_set (&self->priv->view_weakref, view);

	locale = e_book_backend_dup_locale (backend);

	e_data_book_view_watcher_memory_set_locale (self, locale);

	g_free (locale);

	/* requires initial notifications */
	e_data_book_view_set_force_initial_notifications (view, TRUE);

	self->priv->signal_objects_added_id = g_signal_connect (view, "objects-added",
		G_CALLBACK (e_data_book_view_watcher_memory_objects_added_cb), self);
	self->priv->signal_objects_modified_id = g_signal_connect (view, "objects-modified",
		G_CALLBACK (e_data_book_view_watcher_memory_objects_modified_cb), self);
	self->priv->signal_objects_removed_id = g_signal_connect (view, "objects-removed",
		G_CALLBACK (e_data_book_view_watcher_memory_objects_removed_cb), self);

	return G_OBJECT (self);
}

/**
 * e_data_book_view_watcher_memory_set_locale:
 * @self: an #EDataBookViewWatcherMemory
 * @locale: (nullable): a locale to set, or %NULL
 *
 * Sets a locale to use for sorting. When %NULL, or when cannot
 * use the provided locale, tries to use the system locale.
 *
 * Since: 3.50
 **/
void
e_data_book_view_watcher_memory_set_locale (EDataBookViewWatcherMemory *self,
					    const gchar *locale)
{
	ECollator *collator;

	g_return_if_fail (E_IS_DATA_BOOK_VIEW_WATCHER_MEMORY (self));

	collator = locale ? e_collator_new (locale, NULL) : NULL;

	if (!collator) {
		const gchar * const *locales;
		guint ii;

		locales = g_get_language_names ();

		for (ii = 0; locales[ii] && !collator; ii++) {
			collator = e_collator_new (locales[ii], NULL);
		}
	}

	if (collator) {
		g_mutex_lock (&self->priv->property_lock);

		if (self->priv->collator)
			e_collator_unref (self->priv->collator);
		self->priv->collator = collator;

		g_mutex_unlock (&self->priv->property_lock);

		data_book_view_watcher_memory_update_indices (self);
	}
}

/**
 * e_data_book_view_watcher_memory_take_sort_fields:
 * @self: an #EDataBookViewWatcherMemory
 * @sort_fields: (nullable) (transfer full): an #EBookClientViewSortFields, or %NULL
 *
 * Sets @sort_fields as fields to sort the contacts by. If %NULL,
 * sorts by file-as field. The function assumes ownership of the @sort_fields.
 *
 * Since: 3.50
 **/
void
e_data_book_view_watcher_memory_take_sort_fields (EDataBookViewWatcherMemory *self,
						  EBookClientViewSortFields *sort_fields)
{
	gboolean changed = FALSE;

	g_return_if_fail (E_IS_DATA_BOOK_VIEW_WATCHER_MEMORY (self));

	g_mutex_lock (&self->priv->property_lock);

	if (self->priv->sort_fields != sort_fields) {
		changed = TRUE;

		if (self->priv->sort_fields && sort_fields) {
			guint ii;

			for (ii = 0; self->priv->sort_fields[ii].field != E_CONTACT_FIELD_LAST && sort_fields[ii].field != E_CONTACT_FIELD_LAST; ii++) {
				if (self->priv->sort_fields[ii].field != sort_fields[ii].field ||
				    self->priv->sort_fields[ii].sort_type != sort_fields[ii].sort_type)
					break;
			}

			changed = self->priv->sort_fields[ii].field != E_CONTACT_FIELD_LAST || sort_fields[ii].field != E_CONTACT_FIELD_LAST;
		}

		if (changed) {
			g_clear_pointer (&self->priv->sort_fields, e_book_client_view_sort_fields_free);
			self->priv->sort_fields = sort_fields;

			if (!sort_fields) {
				EBookClientViewSortFields tmp_fields[] = {
					{ E_CONTACT_FILE_AS, E_BOOK_CURSOR_SORT_ASCENDING },
					{ E_CONTACT_FIELD_LAST, E_BOOK_CURSOR_SORT_ASCENDING }
				};

				self->priv->sort_fields = e_book_client_view_sort_fields_copy (tmp_fields);
			}

			self->priv->sort_fields_stamp++;

			g_ptr_array_sort_with_data (self->priv->contacts, data_book_view_watcher_memory_compare_cb, self);
		} else {
			e_book_client_view_sort_fields_free (sort_fields);
		}
	}

	g_mutex_unlock (&self->priv->property_lock);

	if (changed) {
		data_book_view_watcher_memory_update_indices (self);
		data_book_view_watcher_memory_notify_content_changed (self);
	}
}

/**
 * e_data_book_view_watcher_memory_dup_contacts:
 * @self: an #EDataBookViewWatcherMemory
 * @range_start: range start, 0-based
 * @range_length: how many contacts to retrieve
 *
 * Retrieves contacts in the given range. Returns %NULL when the @range_start
 * is out of bounds. The function can return less than @range_length contacts.
 *
 * The returned array should be freed with g_ptr_array_unref(),
 * when no longer needed.
 *
 * Returns: (transfer container) (element-type EContact) (nullable): an array of #EContact-s,
 *    or %NULL, when @range_start is out of bounds.
 *
 * Since: 3.50
 **/
GPtrArray *
e_data_book_view_watcher_memory_dup_contacts (EDataBookViewWatcherMemory *self,
					      guint range_start,
					      guint range_length)
{
	GPtrArray *contacts;
	guint ii;

	g_return_val_if_fail (E_IS_DATA_BOOK_VIEW_WATCHER_MEMORY (self), NULL);

	g_mutex_lock (&self->priv->property_lock);

	if (range_start >= self->priv->contacts->len) {
		g_mutex_unlock (&self->priv->property_lock);
		return NULL;
	}

	contacts = g_ptr_array_new_full (MIN (range_length, self->priv->contacts->len - range_start), g_object_unref);

	for (ii = 0; ii < range_length && ii + range_start < self->priv->contacts->len; ii++) {
		ContactData *cd = g_ptr_array_index (self->priv->contacts, ii + range_start);

		if (cd)
			g_ptr_array_add (contacts, g_object_ref (cd->contact));
	}

	g_mutex_unlock (&self->priv->property_lock);

	return contacts;
}
