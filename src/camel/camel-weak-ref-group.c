/*
 * Copyright (C) 2017 Red Hat, Inc. (www.redhat.com)
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * SECTION: camel-weak-ref-group
 * @include: camel/camel.h
 * @short_description: A weak ref group
 *
 * A #GWeakRef as such is not suitable for large sets, because
 * it causes big performance impact on free. This #CamelWeakRefGroup
 * groups together weak references for the same object to minimize
 * the performance issue of the #GWeakRef.
 **/

#include "evolution-data-server-config.h"

#include <glib.h>

#include "camel-weak-ref-group.h"

struct _CamelWeakRefGroup {
	guint ref_count;
	gpointer object;
};

G_DEFINE_BOXED_TYPE (CamelWeakRefGroup, camel_weak_ref_group, camel_weak_ref_group_ref, camel_weak_ref_group_unref)

typedef struct _ObjectData {
	guint64 use_count;
	GWeakRef weakref;
} ObjectData;

static GHashTable *groups = NULL; /* gpointer ~> ObjectData */
G_LOCK_DEFINE_STATIC (groups);

static ObjectData *
object_data_new (gpointer object)
{
	ObjectData *od;

	od = g_slice_new (ObjectData);
	od->use_count = 1;

	g_weak_ref_init (&od->weakref, object);

	return od;
}

static void
object_data_free (gpointer ptr)
{
	ObjectData *od = ptr;

	if (od) {
		g_weak_ref_set (&od->weakref, NULL);
		g_weak_ref_clear (&od->weakref);
		g_slice_free (ObjectData, od);
	}
}

static void
camel_weak_ref_group_object_disposed_cb (gpointer user_data,
					 GObject *object)
{
	G_LOCK (groups);
	if (groups) {
		g_hash_table_remove (groups, object);
		if (!g_hash_table_size (groups)) {
			g_clear_pointer (&groups, g_hash_table_unref);
		}
	}
	G_UNLOCK (groups);
}

static void
camel_weak_ref_group_set_locked (CamelWeakRefGroup *group,
				 gpointer object)
{
	if (object != group->object) {
		ObjectData *od;

		if (group->object) {
			od = groups ? g_hash_table_lookup (groups, group->object) : NULL;
			/* it can be NULL when it's removed in the camel_weak_ref_group_object_disposed_cb() */
			if (od) {
				od->use_count--;
				if (!od->use_count) {
					GObject *stored_object = g_weak_ref_get (&od->weakref);
					if (stored_object) {
						g_object_weak_unref (group->object, camel_weak_ref_group_object_disposed_cb, NULL);
						g_clear_object (&stored_object);
					}

					g_hash_table_remove (groups, group->object);
				}
			}
		}

		group->object = object;

		if (group->object) {
			if (!groups)
				groups = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, object_data_free);

			od = g_hash_table_lookup (groups, group->object);
			if (od) {
				od->use_count++;
			} else {
				g_object_weak_ref (group->object, camel_weak_ref_group_object_disposed_cb, NULL);
				od = object_data_new (group->object);
				g_hash_table_insert (groups, group->object, od);
			}
		}

		if (groups && !g_hash_table_size (groups)) {
			g_clear_pointer (&groups, g_hash_table_unref);
		}
	}
}

/**
 * camel_weak_ref_group_new:
 *
 * Returns: (transfer full): A new #CamelWeakRefGroup instance, which should
 *    be freed with camel_weak_ref_group_unref() when no longer needed.
 *
 * Since: 3.24
 **/
CamelWeakRefGroup *
camel_weak_ref_group_new (void)
{
	CamelWeakRefGroup *wrg;

	wrg = g_slice_new (CamelWeakRefGroup);
	wrg->ref_count = 1;
	wrg->object = NULL;

	return wrg;
}

/**
 * camel_weak_ref_group_ref:
 * @group: a #CamelWeakRefGroup
 *
 * Increases a reference count of the @group.
 *
 * Returns: the @group
 *
 * Since: 3.24
 **/
CamelWeakRefGroup *
camel_weak_ref_group_ref (CamelWeakRefGroup *group)
{
	g_return_val_if_fail (group != NULL, NULL);

	G_LOCK (groups);

	group->ref_count++;

	G_UNLOCK (groups);

	return group;
}

/**
 * camel_weak_ref_group_unref:
 * @group: a #CamelWeakRefGroup
 *
 * Decreases a reference count of the @group. The @group is
 * freed when the reference count reaches zero.
 *
 * Since: 3.24
 **/
void
camel_weak_ref_group_unref (CamelWeakRefGroup *group)
{
	g_return_if_fail (group != NULL);
	g_return_if_fail (group->ref_count > 0);

	G_LOCK (groups);

	group->ref_count--;

	if (!group->ref_count) {
		camel_weak_ref_group_set_locked (group, NULL);
		g_slice_free (CamelWeakRefGroup, group);
	}

	G_UNLOCK (groups);
}

/**
 * camel_weak_ref_group_set:
 * @group: a #CamelWeakRefGroup
 * @object: (nullable): a #GObject descendant, or %NULL
 *
 * Sets the @object as the object help by this @group. If
 * the @object is %NULL, then unsets any previously set.
 *
 * Since: 3.24
 **/
void
camel_weak_ref_group_set (CamelWeakRefGroup *group,
			  gpointer object)
{
	g_return_if_fail (group != NULL);
	g_return_if_fail (!object || G_IS_OBJECT (object));

	G_LOCK (groups);
	camel_weak_ref_group_set_locked (group, object);
	G_UNLOCK (groups);
}

/**
 * camel_weak_ref_group_get:
 * @group: a #CamelWeakRefGroup
 *
 * Returns: (transfer full) (nullable): A referenced object associated with
 *    @group, or %NULL, when no object had been set to it. Use g_object_unref()
 *    to free it, when no longer needed.
 *
 * Since: 3.24
 **/
gpointer
camel_weak_ref_group_get (CamelWeakRefGroup *group)
{
	gpointer object = NULL;

	g_return_val_if_fail (group != NULL, NULL);

	G_LOCK (groups);

	if (group->object && groups) {
		ObjectData *od = g_hash_table_lookup (groups, group->object);

		object = od ? g_weak_ref_get (&od->weakref) : NULL;
	}

	G_UNLOCK (groups);

	return object;
}
