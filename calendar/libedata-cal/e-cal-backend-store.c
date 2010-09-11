/*-*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-cal-backend-store.c
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Chenthill Palanisamy <pchenthill@novell.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "e-cal-backend-store.h"
#include "e-cal-backend-intervaltree.h"
#include <libedataserver/e-data-server-util.h>

#define E_CAL_BACKEND_STORE_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_CAL_BACKEND_STORE, ECalBackendStorePrivate))

struct _ECalBackendStorePrivate {
	gchar *path;
	EIntervalTree *intervaltree;
	gboolean loaded;
};

enum {
	PROP_0,
	PROP_PATH
};

G_DEFINE_TYPE (ECalBackendStore, e_cal_backend_store, G_TYPE_OBJECT)

static void
cal_backend_store_set_path (ECalBackendStore *store,
                            const gchar *path)
{
	g_return_if_fail (store->priv->path == NULL);
	g_return_if_fail (path != NULL);

	store->priv->path = g_strdup (path);
}

static void
cal_backend_store_set_property (GObject *object,
                                guint property_id,
                                const GValue *value,
                                GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_PATH:
			cal_backend_store_set_path (
				E_CAL_BACKEND_STORE (object),
				g_value_get_string (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
cal_backend_store_get_property (GObject *object,
                                guint property_id,
                                GValue *value,
                                GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_PATH:
			g_value_set_string (
				value, e_cal_backend_store_get_path (
				E_CAL_BACKEND_STORE (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
cal_backend_store_finalize (GObject *object)
{
	ECalBackendStorePrivate *priv;

	priv = E_CAL_BACKEND_STORE_GET_PRIVATE (object);

	g_free (priv->path);
	if (priv->intervaltree) {
		e_intervaltree_destroy (priv->intervaltree);
		priv->intervaltree = NULL;
	}

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_cal_backend_store_parent_class)->finalize (object);
}

static void
e_cal_backend_store_class_init (ECalBackendStoreClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (ECalBackendStorePrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = cal_backend_store_set_property;
	object_class->get_property = cal_backend_store_get_property;
	object_class->finalize = cal_backend_store_finalize;

	g_object_class_install_property (
		object_class,
		PROP_PATH,
		g_param_spec_string (
			"path",
			NULL,
			NULL,
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY));
}

static void
e_cal_backend_store_init (ECalBackendStore *store)
{
	ECalBackendStorePrivate *priv;
	priv = E_CAL_BACKEND_STORE_GET_PRIVATE (store);
	store->priv = priv;
	priv->intervaltree = e_intervaltree_new ();
}

/**
 * e_cal_backend_store_get_path:
 *
 * Since: 2.28
 **/
const gchar *
e_cal_backend_store_get_path (ECalBackendStore *store)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND_STORE (store), NULL);

	return store->priv->path;
}

/**
 * e_cal_backend_store_load:
 *
 * Since: 2.28
 **/
gboolean
e_cal_backend_store_load (ECalBackendStore *store)
{
	ECalBackendStorePrivate *priv;

	g_return_val_if_fail (E_IS_CAL_BACKEND_STORE (store), FALSE);

	priv = E_CAL_BACKEND_STORE_GET_PRIVATE (store);

	if (priv->loaded)
		return TRUE;

	priv->loaded = (E_CAL_BACKEND_STORE_GET_CLASS (store))->load (store);

	return priv->loaded;
}

/**
 * e_cal_backend_store_remove:
 *
 * Since: 2.28
 **/
gboolean
e_cal_backend_store_remove (ECalBackendStore *store)
{
	ECalBackendStorePrivate *priv;
	g_return_val_if_fail (E_IS_CAL_BACKEND_STORE (store), FALSE);

	priv = E_CAL_BACKEND_STORE_GET_PRIVATE (store);
	/* remove interval tree */
	e_intervaltree_destroy (priv->intervaltree);
	priv->intervaltree = NULL;

	return (E_CAL_BACKEND_STORE_GET_CLASS (store))->remove (store);
}

/**
 * e_cal_backend_store_clean:
 *
 * Since: 2.28
 **/
gboolean
e_cal_backend_store_clean (ECalBackendStore *store)
{
	ECalBackendStorePrivate *priv;
	g_return_val_if_fail (E_IS_CAL_BACKEND_STORE (store), FALSE);

	priv = E_CAL_BACKEND_STORE_GET_PRIVATE (store);

	if (priv->intervaltree) {
		e_intervaltree_destroy (priv->intervaltree);
		priv->intervaltree = e_intervaltree_new ();
	}

	return (E_CAL_BACKEND_STORE_GET_CLASS (store))->clean (store);
}

/**
 * e_cal_backend_store_get_component:
 *
 * Since: 2.28
 **/
ECalComponent *
e_cal_backend_store_get_component (ECalBackendStore *store, const gchar *uid, const gchar *rid)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND_STORE (store), NULL);
	g_return_val_if_fail (uid != NULL, NULL);

	return (E_CAL_BACKEND_STORE_GET_CLASS (store))->get_component (store, uid, rid);
}

/**
 * e_cal_backend_store_has_component:
 *
 * Since: 2.28
 **/
gboolean
e_cal_backend_store_has_component (ECalBackendStore *store, const gchar *uid, const gchar *rid)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND_STORE (store), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	return (E_CAL_BACKEND_STORE_GET_CLASS (store))->has_component (store, uid, rid);
}

/**
 * e_cal_backend_store_put_component_with_time_range:
 *
 * Since: 2.32
 **/
gboolean
e_cal_backend_store_put_component_with_time_range (ECalBackendStore *store, ECalComponent *comp, time_t occurence_start, time_t occurence_end)
{
	ECalBackendStorePrivate *priv;
	g_return_val_if_fail (E_IS_CAL_BACKEND_STORE (store), FALSE);
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), FALSE);

	priv = E_CAL_BACKEND_STORE_GET_PRIVATE (store);

	if ((E_CAL_BACKEND_STORE_GET_CLASS (store))->put_component (store, comp)) {
		if (e_intervaltree_insert (priv->intervaltree, occurence_start, occurence_end, comp))
			return TRUE;
	}
	return FALSE;

}

/**
 * e_cal_backend_store_put_component:
 *
 * Since: 2.28
 **/
gboolean
e_cal_backend_store_put_component (ECalBackendStore *store, ECalComponent *comp)
{
	ECalBackendStorePrivate *priv;
	g_return_val_if_fail (E_IS_CAL_BACKEND_STORE (store), FALSE);
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), FALSE);

	priv = E_CAL_BACKEND_STORE_GET_PRIVATE (store);

	return (E_CAL_BACKEND_STORE_GET_CLASS (store))->put_component (store, comp);
}

/**
 * e_cal_backend_store_remove_component:
 *
 * Since: 2.28
 **/
gboolean
e_cal_backend_store_remove_component (ECalBackendStore *store, const gchar *uid, const gchar *rid)
{
	ECalBackendStorePrivate *priv;
	g_return_val_if_fail (E_IS_CAL_BACKEND_STORE (store), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	priv = E_CAL_BACKEND_STORE_GET_PRIVATE (store);

	if ((E_CAL_BACKEND_STORE_GET_CLASS (store))->remove_component (store, uid, rid)) {
		if (e_intervaltree_remove (priv->intervaltree, uid, rid))
			return TRUE;
	}
	return FALSE;
}

/**
 * e_cal_backend_store_get_timezone:
 *
 * Since: 2.28
 **/
const icaltimezone *
e_cal_backend_store_get_timezone (ECalBackendStore *store, const gchar *tzid)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND_STORE (store), NULL);
	g_return_val_if_fail (tzid != NULL, NULL);

	return (E_CAL_BACKEND_STORE_GET_CLASS (store))->get_timezone (store, tzid);
}

/**
 * e_cal_backend_store_put_timezone:
 *
 * Since: 2.28
 **/
gboolean
e_cal_backend_store_put_timezone (ECalBackendStore *store, const icaltimezone *zone)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND_STORE (store), FALSE);
	g_return_val_if_fail (zone != NULL, FALSE);

	return (E_CAL_BACKEND_STORE_GET_CLASS (store))->put_timezone (store, zone);
}

/**
 * e_cal_backend_store_remove_timezone:
 *
 * Since: 2.28
 **/
gboolean
e_cal_backend_store_remove_timezone (ECalBackendStore *store, const gchar *tzid)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND_STORE (store), FALSE);
	g_return_val_if_fail (tzid != NULL, FALSE);

	return (E_CAL_BACKEND_STORE_GET_CLASS (store))->remove_timezone (store, tzid);
}

/**
 * e_cal_backend_store_get_default_timezone:
 *
 * Since: 2.28
 **/
const icaltimezone *
e_cal_backend_store_get_default_timezone (ECalBackendStore *store)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND_STORE (store), NULL);

	return (E_CAL_BACKEND_STORE_GET_CLASS (store))->get_default_timezone (store);
}

/**
 * e_cal_backend_store_set_default_timezone:
 *
 * Since: 2.28
 **/
gboolean
e_cal_backend_store_set_default_timezone (ECalBackendStore *store, const icaltimezone *zone)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND_STORE (store), FALSE);
	g_return_val_if_fail (zone != NULL, FALSE);

	return (E_CAL_BACKEND_STORE_GET_CLASS (store))->set_default_timezone (store, zone);
}

/**
 * e_cal_backend_store_get_components_by_uid:
 *
 * Since: 2.28
 **/
GSList *
e_cal_backend_store_get_components_by_uid (ECalBackendStore *store, const gchar *uid)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND_STORE (store), NULL);
	g_return_val_if_fail (uid != NULL, NULL);

	return (E_CAL_BACKEND_STORE_GET_CLASS (store))->get_components_by_uid (store, uid);
}

/**
 * e_cal_backend_store_get_components:
 *
 * Since: 2.28
 **/
GSList *
e_cal_backend_store_get_components (ECalBackendStore *store)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND_STORE (store), NULL);
	return (E_CAL_BACKEND_STORE_GET_CLASS (store))->get_components (store);
}

/**
 * e_cal_backend_store_get_components_occuring_in_range:
 * @store: An #ECalBackendStore object.
 * @start:
 * @end:
 *
 * Retrieves a list of components stored in the store, that are occuring
 * in time range [start, end].
 *
 * Return value: A list of the components. Each item in the list is
 * an #ECalComponent, which should be freed when no longer needed.
 *
 * Since: 2.32
 */
GSList *
e_cal_backend_store_get_components_occuring_in_range (ECalBackendStore *store, time_t start, time_t end)
{
	ECalBackendStorePrivate *priv;
	GList *l;
	GSList *list = NULL;
	icalcomponent *icalcomp;

	g_return_val_if_fail (store != NULL, NULL);
	g_return_val_if_fail (E_IS_CAL_BACKEND_STORE (store), NULL);

	priv = E_CAL_BACKEND_STORE_GET_PRIVATE (store);

	if (!(l = e_intervaltree_search (priv->intervaltree, start, end)))
                return NULL;

	for (; l != NULL; l = g_list_next (l)) {
		ECalComponent *comp = l->data;
		icalcomp = e_cal_component_get_icalcomponent (comp);
		if (icalcomp) {
			icalcomponent_kind kind;

			kind = icalcomponent_isa (icalcomp);
			if (kind == ICAL_VEVENT_COMPONENT || kind == ICAL_VTODO_COMPONENT || kind == ICAL_VJOURNAL_COMPONENT) {
				list = g_slist_prepend (list, comp);
			}
		}
	}

	return list;
}

/**
 * e_cal_backend_store_get_component_ids:
 *
 * Since: 2.28
 **/
GSList *
e_cal_backend_store_get_component_ids (ECalBackendStore *store)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND_STORE (store), NULL);

	return (E_CAL_BACKEND_STORE_GET_CLASS (store))->get_component_ids (store);
}

/**
 * e_cal_backend_store_get_key_value:
 *
 * Since: 2.28
 **/
const gchar *
e_cal_backend_store_get_key_value (ECalBackendStore *store, const gchar *key)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND_STORE (store), NULL);
	g_return_val_if_fail (key != NULL, NULL);

	return (E_CAL_BACKEND_STORE_GET_CLASS (store))->get_key_value (store, key);
}

/**
 * e_cal_backend_store_put_key_value:
 *
 * Since: 2.28
 **/
gboolean
e_cal_backend_store_put_key_value (ECalBackendStore *store, const gchar *key, const gchar *value)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND_STORE (store), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	return (E_CAL_BACKEND_STORE_GET_CLASS (store))->put_key_value (store, key, value);
}

/**
 * e_cal_backend_store_thaw_changes:
 *
 * Since: 2.28
 **/
void
e_cal_backend_store_thaw_changes (ECalBackendStore *store)
{
	g_return_if_fail (E_IS_CAL_BACKEND_STORE (store));

	(E_CAL_BACKEND_STORE_GET_CLASS (store))->thaw_changes (store);
}

/**
 * e_cal_backend_store_freeze_changes:
 *
 * Since: 2.28
 **/
void
e_cal_backend_store_freeze_changes (ECalBackendStore *store)
{
	g_return_if_fail (E_IS_CAL_BACKEND_STORE (store));

	(E_CAL_BACKEND_STORE_GET_CLASS (store))->freeze_changes (store);
}

/**
 * e_cal_backend_store_interval_tree_add_comp:
 *
 * Since: 2.32
 **/
void
e_cal_backend_store_interval_tree_add_comp (ECalBackendStore *store, ECalComponent *comp, time_t occurence_start, time_t occurence_end)
{
	ECalBackendStorePrivate *priv;
	g_return_if_fail (E_IS_CAL_BACKEND_STORE (store));
	g_return_if_fail (E_IS_CAL_COMPONENT (comp));

	priv = E_CAL_BACKEND_STORE_GET_PRIVATE (store);

	e_intervaltree_insert (priv->intervaltree, occurence_start, occurence_end, comp);
}
