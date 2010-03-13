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

G_DEFINE_TYPE (ECalBackendStore, e_cal_backend_store, G_TYPE_OBJECT)

#define GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), E_TYPE_CAL_BACKEND_STORE, ECalBackendStorePrivate))

struct _ECalBackendStorePrivate {
	ECalSourceType source_type;
	gchar *uri;
	gchar *path;
	gboolean loaded;
};

/* Property IDs */
enum {
	PROP_0,
	PROP_SOURCE_TYPE,
	PROP_URI
};

static const gchar *
get_component (ECalSourceType source_type)
{
	switch (source_type) {
		case E_CAL_SOURCE_TYPE_EVENT :
			return "calendar";
		case E_CAL_SOURCE_TYPE_TODO :
			return "tasks";
		case E_CAL_SOURCE_TYPE_JOURNAL :
			return "journal";
		case E_CAL_SOURCE_TYPE_LAST :
		default :
			return "invalid";
	}

}

static void
set_store_path (ECalBackendStore *store)
{
	ECalBackendStorePrivate *priv;

	priv = GET_PRIVATE(store);

	if (priv->uri)
	{
		const gchar *component = get_component (priv->source_type);
		gchar *mangled_uri = NULL;

		mangled_uri = g_strdup (priv->uri);
		mangled_uri = g_strdelimit (mangled_uri, ":/",'_');

		if (priv->path)
			g_free (priv->path);

		priv->path = g_build_filename (g_get_home_dir (), ".evolution/cache/",
				component, mangled_uri, NULL);

		g_free (mangled_uri);
	}
}

static void
set_uri (ECalBackendStore *store, gchar *uri)
{
	ECalBackendStorePrivate *priv;

	priv = GET_PRIVATE(store);

	if (priv->uri)
		g_free (priv->uri);

	priv->uri = uri;
}

static void
e_cal_backend_store_set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *spec)
{
	ECalBackendStore *store;
	ECalBackendStorePrivate *priv;

	store = E_CAL_BACKEND_STORE (object);
	priv = GET_PRIVATE(store);

	switch (property_id) {
	case PROP_SOURCE_TYPE:
		priv->source_type = g_value_get_enum (value);
		break;
	case PROP_URI:
		set_uri (store, g_value_dup_string (value));
		set_store_path (store);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, spec);
	}
}

static void
e_cal_backend_store_get_property (GObject *object, guint property_id, GValue *value, GParamSpec *spec)
{
	ECalBackendStore *store;
	ECalBackendStorePrivate *priv;

	store = E_CAL_BACKEND_STORE (object);
	priv = GET_PRIVATE(store);

	switch (property_id)
	{
	case PROP_SOURCE_TYPE:
		g_value_set_enum (value, priv->source_type);
		break;
	case PROP_URI :
		g_value_set_string (value, priv->uri);
		break;
	default :
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, spec);
	}
}

static void
e_cal_backend_store_dispose (GObject *object)
{
	G_OBJECT_CLASS (e_cal_backend_store_parent_class)->dispose (object);
}

static void
e_cal_backend_store_finalize (GObject *object)
{
	ECalBackendStore *store = (ECalBackendStore *) object;
	ECalBackendStorePrivate *priv;

	priv = GET_PRIVATE(store);

	if (priv->uri) {
		g_free (priv->uri);
		priv->uri = NULL;
	}

	if (priv->path) {
		g_free (priv->path);
		priv->uri = NULL;
	}

	G_OBJECT_CLASS (e_cal_backend_store_parent_class)->finalize (object);
}

static void
e_cal_backend_store_class_init (ECalBackendStoreClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (ECalBackendStorePrivate));

	object_class->dispose = e_cal_backend_store_dispose;
	object_class->finalize = e_cal_backend_store_finalize;
	object_class->set_property = e_cal_backend_store_set_property;
	object_class->get_property = e_cal_backend_store_get_property;

	klass->load = NULL;
	klass->remove = NULL;
	klass->clean = NULL;
	klass->get_component = NULL;
	klass->put_component = NULL;
	klass->remove_component = NULL;
	klass->get_timezone = NULL;
	klass->put_timezone = NULL;
	klass->remove_timezone = NULL;
	klass->get_default_timezone = NULL;
	klass->set_default_timezone = NULL;
	klass->get_components_by_uid = NULL;
	klass->get_key_value = NULL;
	klass->put_key_value = NULL;

	g_object_class_install_property (object_class, PROP_SOURCE_TYPE,
				g_param_spec_enum ("source_type", NULL, NULL,
				e_cal_source_type_enum_get_type (),
				E_CAL_SOURCE_TYPE_EVENT,
				G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class, PROP_URI,
					g_param_spec_string ("uri", NULL, NULL, "",
					G_PARAM_READABLE | G_PARAM_WRITABLE
					| G_PARAM_CONSTRUCT_ONLY));
}

static void
e_cal_backend_store_init (ECalBackendStore *store)
{
	ECalBackendStorePrivate *priv;

	priv = GET_PRIVATE(store);

	store->priv = priv;
	priv->uri = NULL;
	priv->path = NULL;
	priv->source_type = E_CAL_SOURCE_TYPE_EVENT;
	priv->loaded = FALSE;
}

/**
 * e_cal_backend_store_get_path:
 *
 * Since: 2.28
 **/
const gchar *
e_cal_backend_store_get_path (ECalBackendStore *store)
{
	ECalBackendStorePrivate *priv;

	g_return_val_if_fail (store != NULL, NULL);
	g_return_val_if_fail (E_IS_CAL_BACKEND_STORE (store), NULL);

	priv = GET_PRIVATE(store);

	return priv->path;
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

	g_return_val_if_fail (store != NULL, FALSE);
	g_return_val_if_fail (E_IS_CAL_BACKEND_STORE (store), FALSE);

	priv = GET_PRIVATE(store);

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
	g_return_val_if_fail (store != NULL, FALSE);
	g_return_val_if_fail (E_IS_CAL_BACKEND_STORE (store), FALSE);

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
	g_return_val_if_fail (store != NULL, FALSE);
	g_return_val_if_fail (E_IS_CAL_BACKEND_STORE (store), FALSE);

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
	g_return_val_if_fail (store != NULL, NULL);
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
	g_return_val_if_fail (store != NULL, FALSE);
	g_return_val_if_fail (E_IS_CAL_BACKEND_STORE (store), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	return (E_CAL_BACKEND_STORE_GET_CLASS (store))->has_component (store, uid, rid);
}

/**
 * e_cal_backend_store_put_component:
 *
 * Since: 2.28
 **/
gboolean
e_cal_backend_store_put_component (ECalBackendStore *store, ECalComponent *comp)
{
	g_return_val_if_fail (store != NULL, FALSE);
	g_return_val_if_fail (E_IS_CAL_BACKEND_STORE (store), FALSE);
	g_return_val_if_fail (comp != NULL, FALSE);
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp) != FALSE, FALSE);

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
	g_return_val_if_fail (store != NULL, FALSE);
	g_return_val_if_fail (E_IS_CAL_BACKEND_STORE (store), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	return (E_CAL_BACKEND_STORE_GET_CLASS (store))->remove_component (store, uid, rid);
}

/**
 * e_cal_backend_store_get_timezone:
 *
 * Since: 2.28
 **/
const icaltimezone *
e_cal_backend_store_get_timezone (ECalBackendStore *store, const gchar *tzid)
{
	g_return_val_if_fail (store != NULL, NULL);
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
	g_return_val_if_fail (store != NULL, FALSE);
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
	g_return_val_if_fail (store != NULL, FALSE);
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
	g_return_val_if_fail (store != NULL, NULL);
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
	g_return_val_if_fail (store != NULL, FALSE);
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
	g_return_val_if_fail (store != NULL, NULL);
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
	g_return_val_if_fail (store != NULL, NULL);
	g_return_val_if_fail (E_IS_CAL_BACKEND_STORE (store), NULL);

	return (E_CAL_BACKEND_STORE_GET_CLASS (store))->get_components (store);
}

/**
 * e_cal_backend_store_get_component_ids:
 *
 * Since: 2.28
 **/
GSList *
e_cal_backend_store_get_component_ids (ECalBackendStore *store)
{
	g_return_val_if_fail (store != NULL, NULL);
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
	g_return_val_if_fail (store != NULL, NULL);
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
	g_return_val_if_fail (store != NULL, FALSE);
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
	g_return_if_fail (store != NULL);
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
	g_return_if_fail (store != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND_STORE (store));

	(E_CAL_BACKEND_STORE_GET_CLASS (store))->freeze_changes (store);
}
