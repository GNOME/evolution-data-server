/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2019 Red Hat, Inc. (www.redhat.com)
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
 *
 */

#include "evolution-data-server-config.h"

/**
 * SECTION:e-cal-component-id
 * @short_description: An ECalComponentId structure
 * @include: libecal/libecal.h
 *
 * Contains functions to work with the #ECalComponentId structure.
 **/

#include "e-cal-component-id.h"

G_DEFINE_BOXED_TYPE (ECalComponentId, e_cal_component_id, e_cal_component_id_copy, e_cal_component_id_free)

struct _ECalComponentId {
	gchar *uid;
	gchar *rid;
};

/**
 * e_cal_component_id_new:
 * @uid: a unique ID string
 * @rid: (nullable): an optional recurrence ID string
 *
 * Creates a new #ECalComponentId from @uid and @rid, which should be
 * freed with e_cal_component_id_free().
 *
 * Returns: (transfer full): an #ECalComponentId
 *
 * Since: 3.10
 **/
ECalComponentId *
e_cal_component_id_new (const gchar *uid,
                        const gchar *rid)
{
	g_return_val_if_fail (uid != NULL, NULL);

	/* Normalize an empty recurrence ID to NULL. */
	if (rid && !*rid)
		rid = NULL;

	return e_cal_component_id_new_take (g_strdup (uid), g_strdup (rid));
}

/**
 * e_cal_component_id_new_take:
 * @uid: (transfer full): a unique ID string
 * @rid: (transfer full) (nullable): an optional recurrence ID string
 *
 * Creates a new #ECalComponentId from @uid and @rid, which should be
 * freed with e_cal_component_id_free(). The function assumes ownership
 * of @uid and @rid parameters.
 *
 * Returns: (transfer full): an #ECalComponentId
 *
 * Since: 3.34
 **/
ECalComponentId *
e_cal_component_id_new_take (gchar *uid,
			     gchar *rid)
{
	ECalComponentId *id;

	g_return_val_if_fail (uid != NULL, NULL);

	/* Normalize an empty recurrence ID to NULL. */
	if (rid && !*rid) {
		g_free (rid);
		rid = NULL;
	}

	id = g_slice_new0 (ECalComponentId);
	id->uid = uid;
	id->rid = rid;

	return id;
}

/**
 * e_cal_component_id_copy:
 * @id: (not nullable): an #ECalComponentId
 *
 * Returns a newly allocated copy of @id, which should be freed with
 * e_cal_component_id_free().
 *
 * Returns: (transfer full): a newly allocated copy of @id
 *
 * Since: 3.10
 **/
ECalComponentId *
e_cal_component_id_copy (const ECalComponentId *id)
{
	g_return_val_if_fail (id != NULL, NULL);

	return e_cal_component_id_new (id->uid, id->rid);
}

/**
 * e_cal_component_id_free: (skip)
 * @id: (type ECalComponentId) (transfer full) (nullable): an #ECalComponentId
 *
 * Free the @id, previously created by e_cal_component_id_new(),
 * e_cal_component_id_new_take() or e_cal_component_id_copy().
 **/
void
e_cal_component_id_free (gpointer id)
{
	ECalComponentId *eid = id;

	if (eid) {
		g_free (eid->uid);
		g_free (eid->rid);
		g_slice_free (ECalComponentId, eid);
	}
}

/**
 * e_cal_component_id_hash:
 * @id: (type ECalComponentId): an #ECalComponentId
 *
 * Generates a hash value for @id.
 *
 * Returns: a hash value for @id
 *
 * Since: 3.10
 **/
guint
e_cal_component_id_hash (gconstpointer id)
{
	const ECalComponentId *eid = id;
	guint uid_hash;
	guint rid_hash;

	g_return_val_if_fail (id != NULL, 0);

	uid_hash = g_str_hash (eid->uid);
	rid_hash = eid->rid ? g_str_hash (eid->rid) : 0;

	return uid_hash ^ rid_hash;
}

/**
 * e_cal_component_id_equal:
 * @id1: (type ECalComponentId): the first #ECalComponentId
 * @id2: (type ECalComponentId): the second #ECalComponentId
 *
 * Compares two #ECalComponentId structs for equality.
 *
 * Returns: %TRUE if @id1 and @id2 are equal
 *
 * Since: 3.10
 **/
gboolean
e_cal_component_id_equal (gconstpointer id1,
                          gconstpointer id2)
{
	const ECalComponentId *eid1 = id1, *eid2 = id2;
	gboolean uids_equal;
	gboolean rids_equal;

	if (id1 == id2)
		return TRUE;

	/* Safety check before we dereference. */
	g_return_val_if_fail (id1 != NULL, FALSE);
	g_return_val_if_fail (id2 != NULL, FALSE);

	uids_equal = (g_strcmp0 (eid1->uid, eid2->uid) == 0);
	rids_equal = (g_strcmp0 (eid1->rid, eid2->rid) == 0);

	return uids_equal && rids_equal;
}

/**
 * e_cal_component_id_get_uid:
 * @id: an #ECalComponentId
 *
 * Returns: (transfer none): The UID part of the @id. The returned
 *    string is owned by @id and it's valid until it's changed
 *    with e_cal_component_id_set_uid() or until the @id is freed.
 *
 * Since: 3.34
 **/
const gchar *
e_cal_component_id_get_uid (const ECalComponentId *id)
{
	g_return_val_if_fail (id != NULL, NULL);

	return id->uid;
}

/**
 * e_cal_component_id_set_uid:
 * @id: an #ECalComponentId
 * @uid: (not nullable): the UID to set
 *
 * Sets the UID part of the @id.
 *
 * Since: 3.34
 **/
void
e_cal_component_id_set_uid (ECalComponentId *id,
			    const gchar *uid)
{
	g_return_if_fail (id != NULL);
	g_return_if_fail (uid != NULL);

	if (g_strcmp0 (id->uid, uid) != 0) {
		g_free (id->uid);
		id->uid = g_strdup (uid);
	}
}

/**
 * e_cal_component_id_get_rid:
 * @id: an #ECalComponentId
 *
 * Returns: (transfer none) (nullable): The RECURRENCE-ID part of the @id.
 *    The returned string is owned by @id and it's valid until it's
 *    changed with e_cal_component_id_set_rid() or until the @id is freed.
 *
 * Since: 3.34
 **/
const gchar *
e_cal_component_id_get_rid (const ECalComponentId *id)
{
	g_return_val_if_fail (id != NULL, NULL);

	return id->rid;
}

/**
 * e_cal_component_id_set_rid:
 * @id: an #ECalComponentId
 * @rid: (nullable): the RECURRENCE-ID to set
 *
 * Sets the RECURRENCE-ID part of the @id. The @rid can be %NULL
 * or an empty string, where both are treated as %NULL, which
 * means the @id has not RECURRENCE-ID.
 *
 * Since: 3.34
 **/
void
e_cal_component_id_set_rid (ECalComponentId *id,
			    const gchar *rid)
{
	g_return_if_fail (id != NULL);

	if (g_strcmp0 (id->rid, rid) != 0) {
		g_free (id->rid);
		id->rid = (rid && *rid) ? g_strdup (rid) : NULL;
	}
}
