/*
 * SPDX-FileCopyrightText: (C) 2023 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-data-server-config.h"

#include <glib.h>

#include <libedataserver/libedataserver.h>

#include "e-book-backend.h"
#include "e-book-cache.h"
#include "e-data-book-view.h"

#include "e-data-book-view-watcher-cache.h"

/**
 * SECTION: e-data-book-view-watcher-cache
 * @include: libedata-book/libedata-book.h
 * @short_description: Watch #EDataBookView changes with contacts in #EBookCache
 *
 * A structure used to handle "manual query" views for #EBookBackend
 * descendants which use #EBookCache to store the contacts.
 *
 * See %E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY for what it means "manual query" view.
 **/

struct _EDataBookViewWatcherCachePrivate {
	GWeakRef backend_weakref; /* EBookBackend * */
	GWeakRef view_weakref; /* EDataBookView * */
	GWeakRef cache_weakref; /* EBookCache * */
	GMutex property_lock;

	gulong signal_objects_added_id;
	gulong signal_objects_modified_id;
	gulong signal_objects_removed_id;

	guint n_total;

	EBookClientViewSortFields *sort_fields;
};

G_DEFINE_TYPE_WITH_PRIVATE (EDataBookViewWatcherCache, e_data_book_view_watcher_cache, E_TYPE_BOOK_INDICES_UPDATER)

static EBookBackend *
data_book_view_watcher_cache_ref_backend (EDataBookViewWatcherCache *self)
{
	return g_weak_ref_get (&self->priv->backend_weakref);
}

static EDataBookView *
data_book_view_watcher_cache_ref_view (EDataBookViewWatcherCache *self)
{
	return g_weak_ref_get (&self->priv->view_weakref);
}

static EBookCache *
data_book_view_watcher_cache_ref_cache (EDataBookViewWatcherCache *self)
{
	return g_weak_ref_get (&self->priv->cache_weakref);
}

static void
data_book_view_watcher_cache_update_n_total (EDataBookViewWatcherCache *self,
					     guint n_total)
{
	EBookBackend *backend;
	EDataBookView *view;

	backend = data_book_view_watcher_cache_ref_backend (self);
	view = data_book_view_watcher_cache_ref_view (self);

	if (backend && view)
		e_book_backend_set_view_n_total (backend, e_data_book_view_get_id (view), n_total);

	g_clear_object (&backend);
	g_clear_object (&view);
}

static void
data_book_view_watcher_cache_update_indices (EDataBookViewWatcherCache *self)
{
	EBookIndicesUpdater *indices_updater = E_BOOK_INDICES_UPDATER (self);
	EBookBackend *backend;
	EDataBookView *view;
	EBookCache *cache;
	ECollator *collator;

	cache = data_book_view_watcher_cache_ref_cache (self);
	if (cache)
		e_cache_lock (E_CACHE (cache), E_CACHE_LOCK_READ);

	backend = data_book_view_watcher_cache_ref_backend (self);
	view = data_book_view_watcher_cache_ref_view (self);
	collator = cache ? e_book_cache_ref_collator (cache) : NULL;

	g_mutex_lock (&self->priv->property_lock);

	e_book_indices_updater_take_indices (indices_updater, NULL);

	if (collator) {
		const gchar * const *labels;
		gint n_labels = 0;

		labels = e_collator_get_index_labels (collator, &n_labels, NULL, NULL, NULL);

		if (labels && n_labels > 0) {
			EBookIndices *indices;
			GPtrArray *uids = NULL, *values = NULL;
			guint ii;

			indices = g_new0 (EBookIndices, n_labels + 1);

			for (ii = 0; ii < n_labels; ii++) {
				indices[ii].chr = g_strdup (labels[ii]);
				indices[ii].index = G_MAXUINT;
			}

			e_book_indices_set_ascending_sort (indices_updater, !self->priv->sort_fields ||
				self->priv->sort_fields[0].sort_type == E_BOOK_CURSOR_SORT_ASCENDING);
			e_book_indices_updater_take_indices (indices_updater, indices);

			if (!e_book_cache_dup_query_field (cache, self->priv->sort_fields ? self->priv->sort_fields[0].field : E_CONTACT_FILE_AS,
				e_book_backend_sexp_text (e_data_book_view_get_sexp (view)),
				self->priv->sort_fields ? self->priv->sort_fields[0].field : E_CONTACT_FILE_AS,
				self->priv->sort_fields ? self->priv->sort_fields[0].sort_type : E_BOOK_CURSOR_SORT_ASCENDING,
				0, 0, &uids, &values, NULL, NULL) && self->priv->sort_fields) {
				e_book_cache_dup_query_field (cache, E_CONTACT_FILE_AS,
					e_book_backend_sexp_text (e_data_book_view_get_sexp (view)),
					E_CONTACT_FILE_AS,
					E_BOOK_CURSOR_SORT_ASCENDING,
					0, 0, &uids, &values, NULL, NULL);
			}

			if (uids && values && uids->len == values->len) {
				for (ii = 0; ii < uids->len; ii++) {
					const gchar *uid = g_ptr_array_index (uids, ii);
					const gchar *value = g_ptr_array_index (values, ii);
					gint indices_index = e_collator_get_index (collator, value ? value : "");

					e_book_indices_updater_add (indices_updater, uid, indices_index);
					e_data_book_view_claim_contact_uid (view, uid);
				}
			}

			g_clear_pointer (&uids, g_ptr_array_unref);
			g_clear_pointer (&values, g_ptr_array_unref);
		}
	}

	if (backend && view)
		e_book_backend_set_view_indices (backend, e_data_book_view_get_id (view), e_book_indices_updater_get_indices (indices_updater));

	g_mutex_unlock (&self->priv->property_lock);

	if (cache)
		e_cache_unlock (E_CACHE (cache), E_CACHE_UNLOCK_NONE);

	g_clear_pointer (&collator, e_collator_unref);
	g_clear_object (&backend);
	g_clear_object (&view);
	g_clear_object (&cache);
}

static void
data_book_view_watcher_cache_notify_content_changed (EDataBookViewWatcherCache *self)
{
	EDataBookView *view;

	view = data_book_view_watcher_cache_ref_view (self);

	if (view) {
		e_data_book_view_notify_content_changed (view);
		g_object_unref (view);
	}
}

static void
data_book_view_watcher_cache_notify_indices_changed_locked (EDataBookViewWatcherCache *self)
{
	EBookBackend *backend;
	EDataBookView *view;

	backend = data_book_view_watcher_cache_ref_backend (self);
	view = data_book_view_watcher_cache_ref_view (self);

	if (backend && view)
		e_book_backend_set_view_indices (backend, e_data_book_view_get_id (view), e_book_indices_updater_get_indices (E_BOOK_INDICES_UPDATER (self)));

	g_clear_object (&backend);
	g_clear_object (&view);
}

static gint
data_book_view_watcher_cache_get_contact_indices_index_locked (EDataBookViewWatcherCache *self,
							       EBookCache *cache,
							       ECollator *collator,
							       const gchar *uid,
							       G_GNUC_UNUSED const gchar *vcard)
{
	gchar *value;
	guint indices_index = G_MAXINT;

	if (!cache || !collator || !uid)
		return indices_index;

	if (!e_book_cache_dup_summary_field (cache, self->priv->sort_fields ? self->priv->sort_fields[0].field : E_CONTACT_FILE_AS,
		uid, &value, NULL, NULL) && self->priv->sort_fields) {
		e_book_cache_dup_summary_field (cache, E_CONTACT_FILE_AS, uid, &value, NULL, NULL);
	}

	if (value)
		indices_index = e_collator_get_index (collator, value ? value : "");

	g_free (value);

	return indices_index;
}

static void
e_data_book_view_watcher_cache_objects_added_cb (EDataBookView *view,
						  const gchar * const *vcard_uids,
						  gpointer user_data)
{
	EDataBookViewWatcherCache *self = user_data;
	EBookIndicesUpdater *indices_updater = E_BOOK_INDICES_UPDATER (self);
	EBookCache *cache;
	ECollator *collator = NULL;
	gboolean indices_changed = FALSE;
	guint ii, n_total;

	g_return_if_fail (E_IS_DATA_BOOK_VIEW_WATCHER_CACHE (self));
	g_return_if_fail (vcard_uids != NULL);

	cache = data_book_view_watcher_cache_ref_cache (self);
	if (cache) {
		e_cache_lock (E_CACHE (cache), E_CACHE_LOCK_READ);

		collator = e_book_cache_ref_collator (cache);
	}

	g_mutex_lock (&self->priv->property_lock);

	n_total = self->priv->n_total;

	for (ii = 0; vcard_uids[ii] && vcard_uids[ii + 1]; ii += 2) {
		const gchar *vcard, *uid;
		gint indices_index;

		vcard = vcard_uids[ii];
		uid = vcard_uids[ii + 1];

		if (!uid) {
			g_warn_if_reached ();
			break;
		}

		indices_index = data_book_view_watcher_cache_get_contact_indices_index_locked (self, cache, collator, uid, vcard);
		if (indices_index != G_MAXINT) {
			indices_changed = e_book_indices_updater_add (indices_updater, uid, indices_index) || indices_changed;
			n_total++;
		}
	}

	self->priv->n_total = n_total;

	if (indices_changed)
		data_book_view_watcher_cache_notify_indices_changed_locked (self);

	g_mutex_unlock (&self->priv->property_lock);

	if (cache)
		e_cache_unlock (E_CACHE (cache), E_CACHE_UNLOCK_NONE);

	data_book_view_watcher_cache_update_n_total (self, n_total);
	data_book_view_watcher_cache_notify_content_changed (self);

	g_clear_pointer (&collator, e_collator_unref);
	g_clear_object (&cache);
}

static void
e_data_book_view_watcher_cache_objects_modified_cb (EDataBookView *view,
						     const gchar * const *vcard_uids,
						     gpointer user_data)
{
	EDataBookViewWatcherCache *self = user_data;
	EBookIndicesUpdater *indices_updater = E_BOOK_INDICES_UPDATER (self);
	EBookCache *cache;
	ECollator *collator = NULL;
	gboolean content_changed = FALSE;
	gboolean indices_changed = FALSE;
	guint ii;

	g_return_if_fail (E_IS_DATA_BOOK_VIEW_WATCHER_CACHE (self));
	g_return_if_fail (vcard_uids != NULL);

	cache = data_book_view_watcher_cache_ref_cache (self);
	if (cache) {
		e_cache_lock (E_CACHE (cache), E_CACHE_LOCK_READ);

		collator = e_book_cache_ref_collator (cache);
	}

	g_mutex_lock (&self->priv->property_lock);

	for (ii = 0; vcard_uids[ii] && vcard_uids[ii + 1]; ii += 2) {
		const gchar *vcard, *uid;
		gint indices_index;

		vcard = vcard_uids[ii];
		uid = vcard_uids[ii + 1];

		if (!uid) {
			g_warn_if_reached ();
			break;
		}

		/* maybe the sort order did not change, but the contact did change */
		content_changed = TRUE;

		indices_index = data_book_view_watcher_cache_get_contact_indices_index_locked (self, cache, collator, uid, vcard);
		if (indices_index != G_MAXINT)
			indices_changed = e_book_indices_updater_add (indices_updater, uid, indices_index) || indices_changed;
	}

	if (indices_changed)
		data_book_view_watcher_cache_notify_indices_changed_locked (self);

	g_mutex_unlock (&self->priv->property_lock);

	if (cache)
		e_cache_unlock (E_CACHE (cache), E_CACHE_UNLOCK_NONE);

	if (content_changed)
		data_book_view_watcher_cache_notify_content_changed (self);

	g_clear_pointer (&collator, e_collator_unref);
	g_clear_object (&cache);
}

static void
e_data_book_view_watcher_cache_objects_removed_cb (EDataBookView *view,
						    const gchar * const *uids,
						    gpointer user_data)
{
	EDataBookViewWatcherCache *self = user_data;
	EBookIndicesUpdater *indices_updater = E_BOOK_INDICES_UPDATER (self);
	gboolean content_changed = FALSE;
	gboolean indices_changed = FALSE;
	guint ii, n_total;

	g_return_if_fail (E_IS_DATA_BOOK_VIEW_WATCHER_CACHE (self));
	g_return_if_fail (uids != NULL);

	g_mutex_lock (&self->priv->property_lock);

	n_total = self->priv->n_total;

	for (ii = 0; uids[ii]; ii++) {
		content_changed = TRUE;
		indices_changed = e_book_indices_updater_remove (indices_updater, uids[ii]) || indices_changed;
		n_total--;
	}

	self->priv->n_total = n_total;

	if (indices_changed)
		data_book_view_watcher_cache_notify_indices_changed_locked (self);

	g_mutex_unlock (&self->priv->property_lock);

	if (content_changed) {
		data_book_view_watcher_cache_update_n_total (self, n_total);
		data_book_view_watcher_cache_notify_content_changed (self);
	}
}

static void
data_book_view_watcher_cache_dispose (GObject *object)
{
	EDataBookViewWatcherCache *self = E_DATA_BOOK_VIEW_WATCHER_CACHE (object);
	EDataBookView *view;

	view = data_book_view_watcher_cache_ref_view (self);
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
	g_weak_ref_set (&self->priv->cache_weakref, NULL);
	g_clear_pointer (&self->priv->sort_fields, e_book_client_view_sort_fields_free);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_data_book_view_watcher_cache_parent_class)->dispose (object);
}

static void
data_book_view_watcher_cache_finalize (GObject *object)
{
	EDataBookViewWatcherCache *self = E_DATA_BOOK_VIEW_WATCHER_CACHE (object);

	g_weak_ref_clear (&self->priv->backend_weakref);
	g_weak_ref_clear (&self->priv->view_weakref);
	g_weak_ref_clear (&self->priv->cache_weakref);
	g_mutex_clear (&self->priv->property_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_data_book_view_watcher_cache_parent_class)->finalize (object);
}

static void
e_data_book_view_watcher_cache_class_init (EDataBookViewWatcherCacheClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->dispose = data_book_view_watcher_cache_dispose;
	object_class->finalize = data_book_view_watcher_cache_finalize;
}

static void
e_data_book_view_watcher_cache_init (EDataBookViewWatcherCache *self)
{
	self->priv = e_data_book_view_watcher_cache_get_instance_private (self);

	g_weak_ref_init (&self->priv->backend_weakref, NULL);
	g_weak_ref_init (&self->priv->view_weakref, NULL);
	g_weak_ref_init (&self->priv->cache_weakref, NULL);
	g_mutex_init (&self->priv->property_lock);
}

/**
 * e_data_book_view_watcher_cache_new:
 * @backend: an #EBookBackend
 * @cache: an #EBookCache
 * @view: an #EDataBookView
 *
 * Creates a new #EDataBookViewWatcherCache, which will watch the @view
 * and will provide the information about indices and total contacts
 * to the @backend, taking the data from the @cache.
 *
 * Returns: (transfer full): a new #EDataBookViewWatcherCache
 *
 * Since: 3.50
 **/
GObject *
e_data_book_view_watcher_cache_new (EBookBackend *backend,
				    EBookCache *cache,
				    EDataBookView *view)
{
	EDataBookViewWatcherCache *self;
	GError *error = NULL;

	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);
	g_return_val_if_fail (E_IS_BOOK_CACHE (cache), NULL);
	g_return_val_if_fail (E_IS_DATA_BOOK_VIEW (view), NULL);

	self = g_object_new (E_TYPE_DATA_BOOK_VIEW_WATCHER_CACHE, NULL);

	g_weak_ref_set (&self->priv->backend_weakref, backend);
	g_weak_ref_set (&self->priv->cache_weakref, cache);
	g_weak_ref_set (&self->priv->view_weakref, view);

	self->priv->signal_objects_added_id = g_signal_connect (view, "objects-added",
		G_CALLBACK (e_data_book_view_watcher_cache_objects_added_cb), self);
	self->priv->signal_objects_modified_id = g_signal_connect (view, "objects-modified",
		G_CALLBACK (e_data_book_view_watcher_cache_objects_modified_cb), self);
	self->priv->signal_objects_removed_id = g_signal_connect (view, "objects-removed",
		G_CALLBACK (e_data_book_view_watcher_cache_objects_removed_cb), self);

	if (e_book_cache_count_query (cache, e_book_backend_sexp_text (e_data_book_view_get_sexp (view)), &self->priv->n_total, NULL, &error)) {
		e_book_backend_set_view_n_total (backend, e_data_book_view_get_id (view), self->priv->n_total);
	} else {
		g_warning ("%s: Failed to get count of contacts for view: %s", G_STRFUNC, error ? error->message : "Unknown error");
		g_clear_error (&error);
	}

	return G_OBJECT (self);
}

/**
 * e_data_book_view_watcher_cache_take_sort_fields:
 * @self: an #EDataBookViewWatcherCache
 * @sort_fields: (nullable) (transfer full): an #EBookClientViewSortFields, or %NULL
 *
 * Sets @sort_fields as fields to sort the contacts by. If %NULL,
 * sorts by file-as field. The function assumes ownership of the @sort_fields.
 *
 * Since: 3.50
 **/
void
e_data_book_view_watcher_cache_take_sort_fields (EDataBookViewWatcherCache *self,
						  EBookClientViewSortFields *sort_fields)
{
	gboolean changed = FALSE;

	g_return_if_fail (E_IS_DATA_BOOK_VIEW_WATCHER_CACHE (self));

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
		} else {
			e_book_client_view_sort_fields_free (sort_fields);
		}
	}

	g_mutex_unlock (&self->priv->property_lock);

	if (changed) {
		data_book_view_watcher_cache_update_indices (self);
		data_book_view_watcher_cache_notify_content_changed (self);
	}
}

/**
 * e_data_book_view_watcher_cache_dup_contacts:
 * @self: an #EDataBookViewWatcherCache
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
e_data_book_view_watcher_cache_dup_contacts (EDataBookViewWatcherCache *self,
					      guint range_start,
					      guint range_length)
{
	GPtrArray *contacts = NULL;
	EDataBookView *view;
	EBookCache *cache;

	g_return_val_if_fail (E_IS_DATA_BOOK_VIEW_WATCHER_CACHE (self), NULL);

	view = data_book_view_watcher_cache_ref_view (self);
	cache = data_book_view_watcher_cache_ref_cache (self);

	if (view && cache) {
		GError *error = NULL;

		e_cache_lock (E_CACHE (cache), E_CACHE_LOCK_READ);

		g_mutex_lock (&self->priv->property_lock);

		if (range_start >= self->priv->n_total) {
			g_mutex_unlock (&self->priv->property_lock);
			e_cache_unlock (E_CACHE (cache), E_CACHE_UNLOCK_NONE);
			g_clear_object (&cache);
			g_clear_object (&view);
			return NULL;
		}

		if (!e_book_cache_dup_query_contacts (cache, e_book_backend_sexp_text (e_data_book_view_get_sexp (view)),
			self->priv->sort_fields ? self->priv->sort_fields[0].field : E_CONTACT_FILE_AS,
			self->priv->sort_fields ? self->priv->sort_fields[0].sort_type : E_BOOK_CURSOR_SORT_ASCENDING,
			range_start, range_length, &contacts, NULL, &error)) {
			g_warning ("%s: Failed to get contacts for range from:%u len:%u : %s", G_STRFUNC, range_start, range_length,
				error ? error->message : "Unknown error");
			g_clear_error (&error);

			if (!e_book_cache_dup_query_contacts (cache, e_book_backend_sexp_text (e_data_book_view_get_sexp (view)),
				E_CONTACT_FILE_AS,
				E_BOOK_CURSOR_SORT_ASCENDING,
				range_start, range_length, &contacts, NULL, &error)) {
				g_warning ("%s: Failed to get contacts in fallback sort for range from:%u len:%u : %s", G_STRFUNC, range_start, range_length,
					error ? error->message : "Unknown error");
				g_clear_error (&error);
			}
		}

		g_mutex_unlock (&self->priv->property_lock);

		e_cache_unlock (E_CACHE (cache), E_CACHE_UNLOCK_NONE);
	}

	g_clear_object (&cache);
	g_clear_object (&view);

	return contacts;
}
