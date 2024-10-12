/*
 * SPDX-FileCopyrightText: (C) 2023 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-data-server-config.h"

#include <glib.h>

#include <libedataserver/libedataserver.h>

#include "e-book-contacts-utils.h"

#include "e-book-indices-updater.h"

/**
 * SECTION: e-book-indices-updater
 * @include: libebook-contacts/libebook-contacts.h
 * @short_description: Handle #EBookIndices updates
 *
 * This is an abstract class to handle #EBooKindices updates. It's suitable
 * as a base class for objects handling manual query views (see %E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY).
 *
 * The users of this class should call e_book_indices_updater_take_indices() first,
 * thus the #EBookIndices knows the indices. The indices are subsequently updated
 * by e_book_indices_updater_add(), e_book_indices_updater_modify() and
 * e_book_indices_updater_remove() functions, where each returns whether
 * the indices changed.
 *
 * Current indices can be obtained by e_book_indices_updater_get_indices().
 *
 * Note the class is not thread safe. It's a responsibility of the caller
 * to ensure thread safety.
 **/

struct _EBookIndicesUpdaterPrivate {
	GHashTable *uid_to_index; /* gchar * ~> guint */
	EBookIndices *indices;
	guint *indices_count; /* how many contacts are placed in respective 'indices' item */
	guint n_indices; /* for easier boundary checks */
	gboolean ascending_sort;
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (EBookIndicesUpdater, e_book_indices_updater, G_TYPE_OBJECT)

static guint
book_indices_updater_get_previous_count (EBookIndicesUpdater *self,
					 guint before_index) /* not inclusive */
{
	guint ii;

	if ((self->priv->ascending_sort && before_index == 0) ||
	    (!self->priv->ascending_sort && (self->priv->n_indices == 0 || before_index >= self->priv->n_indices - 1)))
		return 0;

	if (self->priv->ascending_sort) {
		for (ii = 0; ii < before_index; ii++) {
			guint idx = before_index - ii - 1;

			if (self->priv->indices[idx].index != G_MAXUINT)
				return self->priv->indices[idx].index + self->priv->indices_count[idx];
		}
	} else if (self->priv->n_indices > 0) {
		for (ii = before_index + 1; ii < self->priv->n_indices; ii++) {
			if (self->priv->indices[ii].index != G_MAXUINT)
				return self->priv->indices[ii].index + self->priv->indices_count[ii];
		}
	}

	return 0;
}

static void
book_indices_updater_dispose (GObject *object)
{
	EBookIndicesUpdater *self = E_BOOK_INDICES_UPDATER (object);

	self->priv->n_indices = 0;
	g_clear_pointer (&self->priv->indices, e_book_indices_free);
	g_clear_pointer (&self->priv->indices_count, g_free);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_book_indices_updater_parent_class)->dispose (object);
}

static void
book_indices_updater_finalize (GObject *object)
{
	EBookIndicesUpdater *self = E_BOOK_INDICES_UPDATER (object);

	g_hash_table_destroy (self->priv->uid_to_index);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_book_indices_updater_parent_class)->finalize (object);
}

static void
e_book_indices_updater_class_init (EBookIndicesUpdaterClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->dispose = book_indices_updater_dispose;
	object_class->finalize = book_indices_updater_finalize;
}

static void
e_book_indices_updater_init (EBookIndicesUpdater *self)
{
	self->priv = e_book_indices_updater_get_instance_private (self);
	self->priv->uid_to_index = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	self->priv->ascending_sort = TRUE;
}

/**
 * e_book_indices_updater_take_indices:
 * @self: an #EBookIndicesUpdater
 * @indices: (nullable) (transfer full): an #EBookIndices, or %NULL
 *
 * Sets the initial indices to be updated by the @self. If %NULL,
 * then unsets them. The function always discards data previously
 * gathered about the involved contacts, regardless whether
 * the indices changed or not.
 *
 * The function assumes ownership of the @indices.
 *
 * Returns: whether the indices changed
 *
 * Since: 3.50
 **/
gboolean
e_book_indices_updater_take_indices (EBookIndicesUpdater *self,
				     EBookIndices *indices)
{
	guint ii;

	g_return_val_if_fail (E_IS_BOOK_INDICES_UPDATER (self), FALSE);

	g_hash_table_remove_all (self->priv->uid_to_index);
	if (self->priv->indices_count) {
		for (ii = 0; ii < self->priv->n_indices; ii++) {
			self->priv->indices_count[ii] = 0;
		}
	}

	if (self->priv->indices == indices)
		return FALSE;

	if (self->priv->indices && indices) {
		for (ii = 0; self->priv->indices[ii].chr != NULL && indices[ii].chr != NULL; ii++) {
			if (self->priv->indices[ii].index != indices[ii].index ||
			    g_strcmp0 (self->priv->indices[ii].chr, indices[ii].chr) != 0)
				break;
		}

		if (!self->priv->indices[ii].chr && !indices[ii].chr) {
			e_book_indices_free (indices);
			return FALSE;
		}
	}

	g_clear_pointer (&self->priv->indices, e_book_indices_free);
	self->priv->indices = indices;

	if (self->priv->indices) {
		for (ii = 0; self->priv->indices[ii].chr != NULL; ii++) {
			/* just count them */
		}

		if (self->priv->n_indices < ii || !self->priv->indices_count) {
			g_clear_pointer (&self->priv->indices_count, g_free);
			if (ii > 0)
				self->priv->indices_count = g_new0 (guint, ii);
		}

		self->priv->n_indices = ii;
	} else {
		self->priv->n_indices = 0;
		g_clear_pointer (&self->priv->indices_count, g_free);
	}

	return TRUE;
}

/**
 * e_book_indices_updater_get_indices:
 * @self: an #EBookIndicesUpdater
 *
 * Sets the initial indices to be updated by the @self. If %NULL,
 * then unsets them.
 *
 * Returns: (nullable): current indices, or %NULL, when none had been set yet
 *
 * Since: 3.50
 **/
const EBookIndices *
e_book_indices_updater_get_indices (EBookIndicesUpdater *self)
{
	g_return_val_if_fail (E_IS_BOOK_INDICES_UPDATER (self), NULL);

	return self->priv->indices;
}

/**
 * e_book_indices_set_ascending_sort:
 * @self: an #EBookIndicesUpdater
 * @ascending_sort: the value to set
 *
 * Sets whether the contacts are sorted in an ascending order; if not,
 * then they are sorted in the descending order. That influences what
 * indexes the indices have set.
 *
 * Since: 3.50
 **/
void
e_book_indices_set_ascending_sort (EBookIndicesUpdater *self,
				   gboolean ascending_sort)
{
	g_return_if_fail (E_IS_BOOK_INDICES_UPDATER (self));

	if ((self->priv->ascending_sort ? 1 : 0) == (ascending_sort ? 1 : 0))
		return;

	self->priv->ascending_sort = ascending_sort;

	if (self->priv->indices && self->priv->n_indices > 0) {
		guint ii, count = 0;

		for (ii = 0; ii < self->priv->n_indices; ii++) {
			guint idx = self->priv->ascending_sort ? ii : (self->priv->n_indices - ii - 1);

			if (self->priv->indices_count[idx] != 0) {
				self->priv->indices[idx].index = count;
				count += self->priv->indices_count[idx];
			}
		}
	}
}

/**
 * e_book_indices_get_ascending_sort:
 * @self: an #EBookIndicesUpdater
 *
 * Returns whether the @self considers contacts stored in the ascending order.
 *
 * Returns: %TRUE, when considers contacts sorted in ascending order,
 *    %FALSE when in the descending order.
 *
 * Since: 3.50
 **/
gboolean
e_book_indices_get_ascending_sort (EBookIndicesUpdater *self)
{
	g_return_val_if_fail (E_IS_BOOK_INDICES_UPDATER (self), FALSE);

	return self->priv->ascending_sort;
}

/**
 * e_book_indices_updater_add:
 * @self: an #EBookIndicesUpdater
 * @uid: a UID of a contact
 * @indices_index: index to the indices array the contact belongs to
 *
 * Notifies the @self that a new contact with UID @uid had been added
 * to the set and it occupies the @indices_index index in the indices.
 * In case the @uid had been added previously its index is modified
 * instead.
 *
 * This function can be used only after initial call to e_book_indices_updater_take_indices().
 *
 * Returns: whether the indices changed
 *
 * Since: 3.50
 **/
gboolean
e_book_indices_updater_add (EBookIndicesUpdater *self,
			    const gchar *uid,
			    guint indices_index)
{
	gboolean changed = FALSE;
	gpointer value = NULL;
	guint ii;

	g_return_val_if_fail (E_IS_BOOK_INDICES_UPDATER (self), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (self->priv->indices != NULL, FALSE);
	g_return_val_if_fail (indices_index < self->priv->n_indices, FALSE);

	if (g_hash_table_lookup_extended (self->priv->uid_to_index, uid, NULL, &value)) {
		guint old_index = GPOINTER_TO_UINT (value);

		if (old_index != indices_index) {
			g_warn_if_fail (self->priv->indices_count[old_index] > 0);

			self->priv->indices_count[old_index]--;
			if (!self->priv->indices_count[old_index]) {
				self->priv->indices[old_index].index = G_MAXUINT;
				changed = TRUE;
			}
		}

		if (old_index < indices_index) {
			for (ii = old_index + (self->priv->ascending_sort ? 1 : 0); ii <= indices_index; ii++) {
				if (self->priv->indices_count[ii] > 0) {
					g_warn_if_fail (self->priv->indices[ii].index != G_MAXUINT);

					if (self->priv->ascending_sort) {
						g_warn_if_fail (self->priv->indices[ii].index > 0);
						self->priv->indices[ii].index--;
						changed = TRUE;
					} else {
						self->priv->indices[ii].index++;
						changed = TRUE;
					}
				}
			}
		} else if (old_index > indices_index) {
			for (ii = indices_index + 1; ii <= old_index; ii++) {
				if (self->priv->indices_count[ii] > 0) {
					g_warn_if_fail (self->priv->indices[ii].index != G_MAXUINT);

					if (self->priv->ascending_sort) {
						g_warn_if_fail (self->priv->indices[ii].index > 0);

						self->priv->indices[ii].index++;
						changed = TRUE;
					} else if (self->priv->indices[ii].index > 0) {
						self->priv->indices[ii].index--;
						changed = TRUE;
					}
				}
			}
		}

		if (old_index != indices_index) {
			gpointer orig_key;

			self->priv->indices_count[indices_index]++;

			if (self->priv->indices_count[indices_index] == 1 || !self->priv->ascending_sort) {
				self->priv->indices[indices_index].index = book_indices_updater_get_previous_count (self, indices_index);
				changed = TRUE;
			}

			if (g_hash_table_steal_extended (self->priv->uid_to_index, uid, &orig_key, NULL)) {
				g_hash_table_insert (self->priv->uid_to_index, orig_key, GUINT_TO_POINTER (indices_index));
			} else {
				g_hash_table_insert (self->priv->uid_to_index, g_strdup (uid), GUINT_TO_POINTER (indices_index));
			}
		}
	} else {
		self->priv->indices_count[indices_index]++;
		g_hash_table_insert (self->priv->uid_to_index, g_strdup (uid), GUINT_TO_POINTER (indices_index));

		if (self->priv->indices_count[indices_index] == 1) {
			self->priv->indices[indices_index].index = book_indices_updater_get_previous_count (self, indices_index);
			changed = TRUE;
		}

		if (self->priv->ascending_sort) {
			for (ii = indices_index + 1; ii < self->priv->n_indices; ii++) {
				if (self->priv->indices[ii].index != G_MAXUINT) {
					self->priv->indices[ii].index++;
					changed = TRUE;
				}
			}
		} else {
			for (ii = 0; ii < indices_index; ii++) {
				if (self->priv->indices[ii].index != G_MAXUINT) {
					self->priv->indices[ii].index++;
					changed = TRUE;
				}
			}
		}
	}

	return changed;
}

/**
 * e_book_indices_updater_remove:
 * @self: an #EBookIndicesUpdater
 * @uid: a UID of a removed contact
 *
 * Notifies the @self that an existing contact with UID @uid had been removed
 * from the set. Calling the function with @uid unknown to the @self does nothing
 * and returns %FALSE.
 *
 * This function can be used only after initial call to e_book_indices_updater_take_indices().
 *
 * Returns: whether the indices changed
 *
 * Since: 3.50
 **/
gboolean
e_book_indices_updater_remove (EBookIndicesUpdater *self,
			       const gchar *uid)
{
	gboolean changed = FALSE;
	gpointer value = NULL;

	g_return_val_if_fail (E_IS_BOOK_INDICES_UPDATER (self), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (self->priv->indices != NULL, FALSE);

	if (g_hash_table_lookup_extended (self->priv->uid_to_index, uid, NULL, &value)) {
		guint old_index = GPOINTER_TO_UINT (value);
		guint ii;

		g_hash_table_remove (self->priv->uid_to_index, uid);

		if (self->priv->indices_count[old_index] > 0) {
			self->priv->indices_count[old_index]--;

			if (!self->priv->indices_count[old_index]) {
				self->priv->indices[old_index].index = G_MAXUINT;
				changed = TRUE;
			}
		}

		if (self->priv->ascending_sort) {
			for (ii = old_index + 1; ii < self->priv->n_indices; ii++) {
				if (self->priv->indices_count[ii] > 0 &&
				    self->priv->indices[ii].index > 0) {
					changed = TRUE;
					self->priv->indices[ii].index--;
				}
			}
		} else {
			for (ii = 0; ii < old_index; ii++) {
				if (self->priv->indices_count[ii] > 0 &&
				    self->priv->indices[ii].index > 0) {
					changed = TRUE;
					self->priv->indices[ii].index--;
				}
			}
		}
	}

	return changed;
}
