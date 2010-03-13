/*-*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-cal-backend-file-store.c
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

#include <string.h>
#include "e-cal-backend-file-store.h"
#include "libebackend/e-file-cache.h"
#include <glib/gstdio.h>

#define CACHE_FILE_NAME "calendar.ics"
#define KEY_FILE_NAME "keys.xml"
#define IDLE_SAVE_TIMEOUT 6000

G_DEFINE_TYPE (ECalBackendFileStore, e_cal_backend_file_store, E_TYPE_CAL_BACKEND_STORE)

#define GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), E_TYPE_CAL_BACKEND_FILE_STORE, ECalBackendFileStorePrivate))

typedef struct {
	ECalComponent *comp;
	GHashTable *recurrences;
} FullCompObject;

struct _ECalBackendFileStorePrivate {
	GHashTable *timezones;
	GHashTable *comp_uid_hash;
	EFileCache *keys_cache;

	GStaticRWLock lock;

	gchar *cache_file_name;
	gchar *key_file_name;

	gboolean dirty;
	gboolean freeze_changes;

	guint save_timeout_id;
};

static void save_cache (ECalBackendFileStore *store);

static FullCompObject *
create_new_full_object (void)
{
	FullCompObject *obj;

	obj = g_new0 (FullCompObject, 1);
	obj->comp = NULL;
	obj->recurrences = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

	return obj;
}

static void
destroy_full_object (FullCompObject *obj)
{
	if (!obj)
		return;

	if (obj->comp)
		g_object_unref (obj->comp);
	obj->comp = NULL;

	g_hash_table_destroy (obj->recurrences);
	obj->recurrences = NULL;

	g_free (obj);
	obj = NULL;
}

static icaltimezone *
copy_timezone (icaltimezone *zone)
{
	icaltimezone *copy;
	icalcomponent *icalcomp;

	copy = icaltimezone_new ();
	icalcomp = icaltimezone_get_component (zone);
	icaltimezone_set_component (copy, icalcomponent_new_clone (icalcomp));

	return copy;
}

static gboolean
put_component (ECalBackendFileStore *fstore, ECalComponent *comp)
{
	ECalBackendFileStorePrivate *priv;
	FullCompObject *obj = NULL;
	const gchar *uid;

	g_return_val_if_fail (comp != NULL, FALSE);

	priv = GET_PRIVATE (fstore);
	e_cal_component_get_uid (comp, &uid);

	if (uid == NULL) {
		g_warning ("The component does not have a valid uid \n");
		return FALSE;
	}

	g_static_rw_lock_writer_lock (&priv->lock);

	obj = g_hash_table_lookup (priv->comp_uid_hash, uid);

	if (obj == NULL) {
		obj = create_new_full_object ();
		g_hash_table_insert (priv->comp_uid_hash, g_strdup (uid), obj);
	}

	if (!e_cal_component_is_instance (comp)) {
		if (obj->comp != NULL)
			g_object_unref (obj->comp);

		obj->comp = comp;
		g_object_ref (comp);
	} else {
		gchar *rid = e_cal_component_get_recurid_as_string (comp);

		g_object_ref (comp);
		g_hash_table_insert (obj->recurrences, rid, comp);
	}

	g_static_rw_lock_writer_unlock (&priv->lock);

	return TRUE;
}

static gboolean
remove_component (ECalBackendFileStore *fstore, const gchar *uid, const gchar *rid)
{
	ECalBackendFileStorePrivate *priv;
	FullCompObject *obj = NULL;
	gboolean ret_val = TRUE;
	gboolean remove_completely = FALSE;

	priv = GET_PRIVATE (fstore);

	g_static_rw_lock_writer_lock (&priv->lock);

	obj = g_hash_table_lookup (priv->comp_uid_hash, uid);
	if (obj == NULL) {
		ret_val = FALSE;
		goto end;
	}

	if (rid != NULL && *rid) {
		ret_val = g_hash_table_remove (obj->recurrences, rid);

		if (ret_val && g_hash_table_size (obj->recurrences) == 0 && !obj->comp)
			remove_completely = TRUE;
	} else
		remove_completely = TRUE;

	if (remove_completely)
		g_hash_table_remove (priv->comp_uid_hash, uid);

end:
	g_static_rw_lock_writer_unlock (&priv->lock);
	return ret_val;
}

static ECalComponent *
get_component (ECalBackendFileStore *fstore, const gchar *uid, const gchar *rid)
{
	ECalBackendFileStorePrivate *priv;
	FullCompObject *obj = NULL;
	ECalComponent *comp = NULL;

	priv = GET_PRIVATE(fstore);

	g_static_rw_lock_reader_lock (&priv->lock);

	obj = g_hash_table_lookup (priv->comp_uid_hash, uid);
	if (obj == NULL)
		goto end;

	if (rid != NULL && *rid)
		comp = g_hash_table_lookup (obj->recurrences, rid);
	else
		comp = obj->comp;

	if (comp != NULL)
		g_object_ref (comp);

end:
	g_static_rw_lock_reader_unlock (&priv->lock);
	return comp;
}

static ECalComponent *
e_cal_backend_file_store_get_component (ECalBackendStore *store, const gchar *uid, const gchar *rid)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);
	ECalBackendFileStorePrivate *priv;

	priv = GET_PRIVATE (fstore);

	return get_component (fstore, uid, rid);
}

static gboolean
e_cal_backend_file_store_has_component (ECalBackendStore *store, const gchar *uid, const gchar *rid)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);
	ECalBackendFileStorePrivate *priv;
	gboolean ret_val = FALSE;
	FullCompObject *obj = NULL;

	priv = GET_PRIVATE (fstore);

	g_static_rw_lock_reader_lock (&priv->lock);

	obj = g_hash_table_lookup (priv->comp_uid_hash, uid);
	if (obj == NULL) {
		goto end;
	}

	if (rid != NULL) {
		ECalComponent *comp = g_hash_table_lookup (obj->recurrences, rid);

		if (comp != NULL)
			ret_val = TRUE;
	} else
		ret_val = TRUE;

end:
	g_static_rw_lock_reader_unlock (&priv->lock);
	return ret_val;
}

static gboolean
e_cal_backend_file_store_put_component (ECalBackendStore *store, ECalComponent *comp)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);
	ECalBackendFileStorePrivate *priv;
	gboolean ret_val = FALSE;

	priv = GET_PRIVATE (fstore);

	ret_val = put_component (fstore, comp);

	if (ret_val) {
		priv->dirty = TRUE;

		if (!priv->freeze_changes)
			save_cache (fstore);
	}

	return ret_val;
}

static gboolean
e_cal_backend_file_store_remove_component (ECalBackendStore *store, const gchar *uid, const gchar *rid)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);
	ECalBackendFileStorePrivate *priv;
	gboolean ret_val = FALSE;

	priv = GET_PRIVATE (fstore);

	ret_val = remove_component (fstore, uid, rid);

	if (ret_val) {
		priv->dirty = TRUE;

		if (!priv->freeze_changes)
			save_cache (fstore);
	}

	return ret_val;
}

static const icaltimezone *
e_cal_backend_file_store_get_timezone (ECalBackendStore *store, const gchar *tzid)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);
	ECalBackendFileStorePrivate *priv;
	const icaltimezone *zone = NULL;

	priv = GET_PRIVATE (fstore);

	g_static_rw_lock_reader_lock (&priv->lock);
	zone = g_hash_table_lookup (priv->timezones, tzid);
	g_static_rw_lock_reader_unlock (&priv->lock);

	return zone;
}

static gboolean
e_cal_backend_file_store_put_timezone (ECalBackendStore *store, const icaltimezone *zone)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);
	ECalBackendFileStorePrivate *priv;
	gboolean ret_val = FALSE;
	icaltimezone *copy;

	priv = GET_PRIVATE (fstore);

	g_static_rw_lock_writer_lock (&priv->lock);
	copy = copy_timezone ((icaltimezone *) zone);
	g_hash_table_insert (priv->timezones, g_strdup (icaltimezone_get_tzid ((icaltimezone *) zone)), copy);
	g_static_rw_lock_writer_unlock (&priv->lock);

	if (ret_val) {
		priv->dirty = TRUE;

		if (!priv->freeze_changes)
			save_cache (fstore);
	}

	return ret_val;
}

static gboolean
e_cal_backend_file_store_remove_timezone (ECalBackendStore *store, const gchar *tzid)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);
	ECalBackendFileStorePrivate *priv;
	gboolean ret_val = FALSE;

	priv = GET_PRIVATE (fstore);

	g_static_rw_lock_writer_lock (&priv->lock);
	ret_val = g_hash_table_remove (priv->timezones, tzid);
	g_static_rw_lock_writer_unlock (&priv->lock);

	if (ret_val) {
		priv->dirty = TRUE;

		if (!priv->freeze_changes)
			save_cache (fstore);
	}

	return ret_val;
}

static const gchar *
e_cal_backend_file_store_get_key_value (ECalBackendStore *store, const gchar *key)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);
	ECalBackendFileStorePrivate *priv;
	const gchar *value;

	priv = GET_PRIVATE (fstore);

	g_static_rw_lock_reader_lock (&priv->lock);
	value = e_file_cache_get_object (priv->keys_cache, key);
	g_static_rw_lock_reader_unlock (&priv->lock);

	return value;
}

static gboolean
e_cal_backend_file_store_put_key_value (ECalBackendStore *store, const gchar *key, const gchar *value)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);
	ECalBackendFileStorePrivate *priv;
	gboolean ret_val = FALSE;

	priv = GET_PRIVATE (fstore);

	g_static_rw_lock_writer_lock (&priv->lock);

	if (e_file_cache_get_object (priv->keys_cache, key))
		ret_val = e_file_cache_replace_object (priv->keys_cache, key, value);
	else
		ret_val = e_file_cache_add_object (priv->keys_cache, key, value);

	g_static_rw_lock_writer_unlock (&priv->lock);

	return ret_val;
}

static const icaltimezone *
e_cal_backend_file_store_get_default_timezone (ECalBackendStore *store)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);
	ECalBackendFileStorePrivate *priv;
	const gchar *tzid;
	const icaltimezone *zone = NULL;

	priv = GET_PRIVATE (fstore);

	g_static_rw_lock_reader_lock (&priv->lock);

	tzid = e_file_cache_get_object (priv->keys_cache, "default-zone");
	if (tzid)
		zone = g_hash_table_lookup (priv->timezones, tzid);

	g_static_rw_lock_reader_unlock (&priv->lock);

	return zone;
}

static gboolean
e_cal_backend_file_store_set_default_timezone (ECalBackendStore *store, const icaltimezone *zone)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);
	ECalBackendFileStorePrivate *priv;
	const gchar *tzid;
	icaltimezone *copy;
	const gchar *key = "default-zone";
	gboolean ret_val;

	priv = GET_PRIVATE (fstore);

	g_static_rw_lock_writer_lock (&priv->lock);

	tzid = icaltimezone_get_tzid ((icaltimezone*) zone);
	copy = copy_timezone ((icaltimezone *) zone);
	g_hash_table_insert (priv->timezones, g_strdup (tzid), copy);

	if (e_file_cache_get_object (priv->keys_cache, key))
		ret_val = e_file_cache_replace_object (priv->keys_cache, key, tzid);
	else
		ret_val = e_file_cache_add_object (priv->keys_cache, key, tzid);

	g_static_rw_lock_writer_unlock (&priv->lock);

	return TRUE;
}

static void
e_cal_backend_file_store_thaw_changes (ECalBackendStore *store)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);
	ECalBackendFileStorePrivate *priv;

	priv = GET_PRIVATE (fstore);

	priv->freeze_changes = FALSE;

	e_file_cache_thaw_changes (priv->keys_cache);
	if (priv->dirty) {
		save_cache (fstore);
	}
}

static void
e_cal_backend_file_store_freeze_changes (ECalBackendStore *store)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);
	ECalBackendFileStorePrivate *priv;

	priv = GET_PRIVATE (fstore);

	priv->freeze_changes = TRUE;
	e_file_cache_freeze_changes (priv->keys_cache);
}

static void
add_comp_to_slist (gpointer key, gpointer value, gpointer user_data)
{
	GSList **slist = (GSList **) user_data;
	ECalComponent *comp = (ECalComponent *) value;

	g_object_ref (comp);
	*slist = g_slist_prepend (*slist, comp);
}

static GSList *
e_cal_backend_file_store_get_components_by_uid (ECalBackendStore *store, const gchar *uid)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);
	ECalBackendFileStorePrivate *priv;
	FullCompObject *obj = NULL;
	GSList *comps = NULL;

	priv = GET_PRIVATE (fstore);

	g_static_rw_lock_reader_lock (&priv->lock);

	obj = g_hash_table_lookup (priv->comp_uid_hash, uid);
	if (obj == NULL) {
		goto end;
	}

	if (obj->comp) {
		g_object_ref (obj->comp);
		comps = g_slist_append (comps, obj->comp);
	}

	g_hash_table_foreach (obj->recurrences, (GHFunc) add_comp_to_slist, &comps);
end:
	g_static_rw_lock_reader_unlock (&priv->lock);
	return comps;
}

static void
add_full_comp_to_slist (gpointer key, gpointer value, gpointer user_data)
{
	GSList **slist = (GSList **) user_data;
	FullCompObject *obj = NULL;

	obj = value;
	if (obj->comp) {
		g_object_ref (obj->comp);
		*slist = g_slist_prepend (*slist, obj->comp);
	}

	g_hash_table_foreach (obj->recurrences, (GHFunc) add_comp_to_slist, slist);
}

static GSList *
e_cal_backend_file_store_get_components (ECalBackendStore *store)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);
	ECalBackendFileStorePrivate *priv;
	GSList *comps = NULL;

	priv = GET_PRIVATE (fstore);

	g_static_rw_lock_reader_lock (&priv->lock);
	g_hash_table_foreach (priv->comp_uid_hash, (GHFunc) add_full_comp_to_slist, &comps);
	g_static_rw_lock_reader_unlock (&priv->lock);

	return comps;
}

static void
add_instance_ids_to_slist (gpointer key, gpointer value, gpointer user_data)
{
	GSList **slist = (GSList **) user_data;
	ECalComponent *comp = (ECalComponent *) value;
	ECalComponentId *id = e_cal_component_get_id (comp);

	*slist = g_slist_prepend (*slist, id);
}

static void
add_comp_ids_to_slist (gpointer key, gpointer value, gpointer user_data)
{
	GSList **slist = (GSList **) user_data;
	FullCompObject *obj = NULL;

	obj = value;
	if (obj->comp) {
		ECalComponentId *id = e_cal_component_get_id (obj->comp);

		*slist = g_slist_prepend (*slist, id);
	}

	g_hash_table_foreach (obj->recurrences, (GHFunc) add_instance_ids_to_slist, slist);
}

static GSList *
e_cal_backend_file_store_get_component_ids (ECalBackendStore *store)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);
	ECalBackendFileStorePrivate *priv;
	GSList *comp_ids = NULL;

	priv = GET_PRIVATE (fstore);

	g_static_rw_lock_reader_lock (&priv->lock);
	g_hash_table_foreach (priv->comp_uid_hash, (GHFunc) add_comp_ids_to_slist, &comp_ids);
	g_static_rw_lock_reader_unlock (&priv->lock);

	return comp_ids;
}

static void
add_timezone (ECalBackendFileStore *fstore, icalcomponent *vtzcomp)
{
	ECalBackendFileStorePrivate *priv;
	icalproperty *prop;
	icaltimezone *zone;
	const gchar *tzid;

	priv = GET_PRIVATE(fstore);

	prop = icalcomponent_get_first_property (vtzcomp, ICAL_TZID_PROPERTY);
	if (!prop)
		return;

	tzid = icalproperty_get_tzid (prop);
	if (g_hash_table_lookup (priv->timezones, tzid))
		return;

	zone = icaltimezone_new ();
	if (!icaltimezone_set_component (zone, icalcomponent_new_clone (vtzcomp))) {
		icaltimezone_free (zone, TRUE);
		return;
	}

	g_static_rw_lock_writer_lock (&priv->lock);
	g_hash_table_insert (priv->timezones, g_strdup (tzid), zone);
	g_static_rw_lock_writer_unlock (&priv->lock);
}

static void
scan_vcalendar (ECalBackendFileStore *fstore, icalcomponent *top_icalcomp)
{
	ECalBackendFileStorePrivate *priv;
	icalcompiter iter;

	priv = GET_PRIVATE(fstore);

	for (iter = icalcomponent_begin_component (top_icalcomp, ICAL_ANY_COMPONENT);
	     icalcompiter_deref (&iter) != NULL;
	     icalcompiter_next (&iter)) {
		icalcomponent *icalcomp;
		icalcomponent_kind kind;
		ECalComponent *comp;

		icalcomp = icalcompiter_deref (&iter);

		kind = icalcomponent_isa (icalcomp);

		if (!(kind == ICAL_VEVENT_COMPONENT
		      || kind == ICAL_VTODO_COMPONENT
		      || kind == ICAL_VJOURNAL_COMPONENT
		      || kind == ICAL_VTIMEZONE_COMPONENT))
			continue;

		if (kind == ICAL_VTIMEZONE_COMPONENT) {
			add_timezone (fstore, icalcomp);
			continue;
		}

		comp = e_cal_component_new ();

		if (!e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (icalcomp))) {
			g_object_unref (comp);
			continue;
		}

		put_component (fstore, comp);

		g_object_unref (comp);
	}
}

static gboolean
e_cal_backend_file_store_load (ECalBackendStore *store)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);
	ECalBackendFileStorePrivate *priv;
	icalcomponent *icalcomp;

	priv = GET_PRIVATE(fstore);

	if (!priv->cache_file_name || !priv->key_file_name)
		return FALSE;

	/* Parse keys */
	priv->keys_cache = e_file_cache_new (priv->key_file_name);

	/* Parse components */
	icalcomp = e_cal_util_parse_ics_file (priv->cache_file_name);
	if (!icalcomp)
		return FALSE;

	if (icalcomponent_isa (icalcomp) != ICAL_VCALENDAR_COMPONENT) {
		icalcomponent_free (icalcomp);

		return FALSE;
	}

	scan_vcalendar (fstore, icalcomp);
	icalcomponent_free (icalcomp);

	return TRUE;
}

static gboolean
e_cal_backend_file_store_remove (ECalBackendStore *store)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);
	ECalBackendFileStorePrivate *priv;

	priv = GET_PRIVATE (fstore);

	/* This will remove all the contents in the directory */
	e_file_cache_remove (priv->keys_cache);

	g_hash_table_destroy (priv->timezones);
	priv->timezones = NULL;

	g_hash_table_destroy (priv->comp_uid_hash);
	priv->comp_uid_hash = NULL;

	return TRUE;
}

static gboolean
e_cal_backend_file_store_clean (ECalBackendStore *store)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);
	ECalBackendFileStorePrivate *priv;

	priv = GET_PRIVATE (store);

	g_static_rw_lock_writer_lock (&priv->lock);

	e_file_cache_clean (priv->keys_cache);
	g_hash_table_remove_all (priv->comp_uid_hash);
	g_hash_table_remove_all (priv->timezones);

	g_static_rw_lock_writer_unlock (&priv->lock);

	save_cache (fstore);
	return TRUE;
}

static void
save_instance (gpointer key, gpointer value, gpointer user_data)
{
	icalcomponent *vcalcomp = user_data;
	icalcomponent *icalcomp;
	ECalComponent *comp = value;

	icalcomp = icalcomponent_new_clone (e_cal_component_get_icalcomponent (comp));
	icalcomponent_add_component (vcalcomp, icalcomp);
}

static void
save_object (gpointer key, gpointer value, gpointer user_data)
{
	FullCompObject *obj = value;
	icalcomponent *icalcomp, *vcalcomp = user_data;

	if (obj->comp) {
		icalcomp = icalcomponent_new_clone (e_cal_component_get_icalcomponent (obj->comp));
		icalcomponent_add_component (vcalcomp, icalcomp);
	}

	g_hash_table_foreach (obj->recurrences, save_instance, vcalcomp);
}

static void
save_timezone (gpointer key, gpointer tz, gpointer vcalcomp)
{
	icalcomponent *tzcomp;

	tzcomp = icalcomponent_new_clone (icaltimezone_get_component (tz));
	icalcomponent_add_component (vcalcomp, tzcomp);
}

static gboolean
timeout_save_cache (gpointer user_data)
{
	ECalBackendFileStore *fstore = user_data;
	ECalBackendFileStorePrivate *priv;
	icalcomponent *vcalcomp;
	gchar *data = NULL, *tmpfile;
	gsize len, nwrote;
	FILE *f;

	priv = GET_PRIVATE(fstore);

	g_static_rw_lock_reader_lock (&priv->lock);

	priv->save_timeout_id = 0;

	vcalcomp = e_cal_util_new_top_level ();
	g_hash_table_foreach (priv->timezones, save_timezone, vcalcomp);
	g_hash_table_foreach (priv->comp_uid_hash, save_object, vcalcomp);
	data = icalcomponent_as_ical_string_r (vcalcomp);
	icalcomponent_free (vcalcomp);

	tmpfile = g_strdup_printf ("%s~", priv->cache_file_name);
	f = g_fopen (tmpfile, "wb");
	if (!f)
		goto error;

	len = strlen (data);
	nwrote = fwrite (data, 1, len, f);
	if (fclose (f) != 0 || nwrote != len)
		goto error;

	if (g_rename (tmpfile, priv->cache_file_name) != 0)
		g_unlink (tmpfile);

	e_file_cache_thaw_changes (priv->keys_cache);

error:
	g_static_rw_lock_reader_unlock (&priv->lock);
	g_free (tmpfile);
	g_free (data);
	return FALSE;
}

static void
save_cache (ECalBackendFileStore *store)
{
	ECalBackendFileStorePrivate *priv;

	priv = GET_PRIVATE(store);

	if (priv->save_timeout_id) {
		g_source_remove (priv->save_timeout_id);
	}

	priv->save_timeout_id = g_timeout_add (IDLE_SAVE_TIMEOUT, timeout_save_cache, store);
}

static void
free_timezone (gpointer data)
{
	icaltimezone *zone = data;

	icaltimezone_free (zone, 1);
}

static void
e_cal_backend_file_store_construct (ECalBackendFileStore *fstore)
{
	ECalBackendFileStorePrivate *priv;
	ECalBackendStore *store = E_CAL_BACKEND_STORE (fstore);
	const gchar *path;

	priv = GET_PRIVATE(store);

	path = e_cal_backend_store_get_path (store);
	priv->cache_file_name = g_build_filename (path, CACHE_FILE_NAME, NULL);
	priv->key_file_name = g_build_filename (path, KEY_FILE_NAME, NULL);
}

static void
e_cal_backend_file_store_dispose (GObject *object)
{
	G_OBJECT_CLASS (e_cal_backend_file_store_parent_class)->dispose (object);
}

static void
e_cal_backend_file_store_finalize (GObject *object)
{
	ECalBackendFileStore *fstore = (ECalBackendFileStore *) object;
	ECalBackendFileStorePrivate *priv;

	priv = GET_PRIVATE(fstore);

	if (priv->save_timeout_id) {
		g_source_remove (priv->save_timeout_id);
		timeout_save_cache (fstore);
		priv->save_timeout_id = 0;
	}

	if (priv->timezones) {
		g_hash_table_destroy (priv->timezones);
		priv->timezones = NULL;
	}

	if (priv->comp_uid_hash) {
		g_hash_table_destroy (priv->comp_uid_hash);
		priv->comp_uid_hash = NULL;
	}

	if (priv->keys_cache) {
		g_object_unref (priv->keys_cache);
		priv->keys_cache = NULL;
	}

	if (priv->cache_file_name) {
		g_free (priv->cache_file_name);
		priv->cache_file_name = NULL;
	}

	if (priv->key_file_name) {
		g_free (priv->key_file_name);
		priv->key_file_name = NULL;
	}

	priv->dirty = FALSE;
	priv->freeze_changes = FALSE;
	g_static_rw_lock_free (&priv->lock);

	G_OBJECT_CLASS (e_cal_backend_file_store_parent_class)->finalize (object);
}

static void
e_cal_backend_file_store_class_init (ECalBackendFileStoreClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ECalBackendStoreClass *store_class = E_CAL_BACKEND_STORE_CLASS (klass);

	g_type_class_add_private (klass, sizeof (ECalBackendFileStorePrivate));

	object_class->dispose = e_cal_backend_file_store_dispose;
	object_class->finalize = e_cal_backend_file_store_finalize;

	store_class->load = e_cal_backend_file_store_load;
	store_class->remove = e_cal_backend_file_store_remove;
	store_class->clean = e_cal_backend_file_store_clean;
	store_class->get_component = e_cal_backend_file_store_get_component;
	store_class->put_component = e_cal_backend_file_store_put_component;
	store_class->remove_component = e_cal_backend_file_store_remove_component;
	store_class->has_component = e_cal_backend_file_store_has_component;
	store_class->get_timezone = e_cal_backend_file_store_get_timezone;
	store_class->put_timezone = e_cal_backend_file_store_put_timezone;
	store_class->remove_timezone = e_cal_backend_file_store_remove_timezone;
	store_class->get_default_timezone = e_cal_backend_file_store_get_default_timezone;
	store_class->set_default_timezone = e_cal_backend_file_store_set_default_timezone;
	store_class->get_components_by_uid = e_cal_backend_file_store_get_components_by_uid;
	store_class->get_key_value = e_cal_backend_file_store_get_key_value;
	store_class->put_key_value = e_cal_backend_file_store_put_key_value;
	store_class->thaw_changes = e_cal_backend_file_store_thaw_changes;
	store_class->freeze_changes = e_cal_backend_file_store_freeze_changes;
	store_class->get_components = e_cal_backend_file_store_get_components;
	store_class->get_component_ids = e_cal_backend_file_store_get_component_ids;
}

static void
e_cal_backend_file_store_init (ECalBackendFileStore *self)
{
	ECalBackendFileStorePrivate *priv;

	priv = GET_PRIVATE(self);

	self->priv = priv;

	priv->timezones = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) free_timezone);
	priv->comp_uid_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) destroy_full_object);
	priv->keys_cache = NULL;
	g_static_rw_lock_init (&priv->lock);
	priv->cache_file_name = NULL;
	priv->key_file_name = NULL;
	priv->dirty = FALSE;
	priv->freeze_changes = FALSE;
	priv->save_timeout_id = 0;
}

/**
 * e_cal_backend_file_store_new:
 *
 * Since: 2.28
 **/
ECalBackendFileStore*
e_cal_backend_file_store_new (const gchar *uri, ECalSourceType source_type)
{
	ECalBackendFileStore *fstore;

	fstore =  g_object_new (E_TYPE_CAL_BACKEND_FILE_STORE, "source_type", source_type, "uri", uri, NULL);
	e_cal_backend_file_store_construct (fstore);

	return fstore;
}
