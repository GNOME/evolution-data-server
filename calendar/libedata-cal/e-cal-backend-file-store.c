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

#include "e-cal-backend-file-store.h"

#include <string.h>
#include <glib/gstdio.h>

#include <libecal/libecal.h>
#include <libebackend/libebackend.h>

#define CACHE_FILE_NAME "calendar.ics"
#define KEY_FILE_NAME "keys.xml"
#define IDLE_SAVE_TIMEOUT 6000

#define E_CAL_BACKEND_FILE_STORE_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_CAL_BACKEND_FILE_STORE, ECalBackendFileStorePrivate))

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

G_DEFINE_TYPE (
	ECalBackendFileStore,
	e_cal_backend_file_store, E_TYPE_CAL_BACKEND_STORE)

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
put_component (ECalBackendFileStore *fstore,
               ECalComponent *comp)
{
	FullCompObject *obj = NULL;
	const gchar *uid;

	g_return_val_if_fail (comp != NULL, FALSE);

	e_cal_component_get_uid (comp, &uid);

	if (uid == NULL) {
		g_warning ("The component does not have a valid uid \n");
		return FALSE;
	}

	g_static_rw_lock_writer_lock (&fstore->priv->lock);
	obj = g_hash_table_lookup (fstore->priv->comp_uid_hash, uid);
	if (obj == NULL) {
		obj = create_new_full_object ();
		g_hash_table_insert (
			fstore->priv->comp_uid_hash, g_strdup (uid), obj);
	}

	if (!e_cal_component_is_instance (comp)) {
		if (obj->comp != NULL)
			g_object_unref (obj->comp);

		obj->comp = comp;
	} else {
		gchar *rid = e_cal_component_get_recurid_as_string (comp);

		g_hash_table_insert (obj->recurrences, rid, comp);
	}

	g_object_ref (comp);
	g_static_rw_lock_writer_unlock (&fstore->priv->lock);

	return TRUE;
}

static gboolean
remove_component (ECalBackendFileStore *fstore,
                  const gchar *uid,
                  const gchar *rid)
{
	FullCompObject *obj = NULL;
	gboolean ret_val = TRUE;
	gboolean remove_completely = FALSE;

	g_static_rw_lock_writer_lock (&fstore->priv->lock);

	obj = g_hash_table_lookup (fstore->priv->comp_uid_hash, uid);
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
		g_hash_table_remove (fstore->priv->comp_uid_hash, uid);

end:
	g_static_rw_lock_writer_unlock (&fstore->priv->lock);

	return ret_val;

}

static ECalComponent *
get_component (ECalBackendFileStore *fstore,
               const gchar *uid,
               const gchar *rid)
{
	FullCompObject *obj = NULL;
	ECalComponent *comp = NULL;

	g_static_rw_lock_reader_lock (&fstore->priv->lock);

	obj = g_hash_table_lookup (fstore->priv->comp_uid_hash, uid);
	if (obj == NULL)
		goto end;

	if (rid != NULL && *rid)
		comp = g_hash_table_lookup (obj->recurrences, rid);
	else
		comp = obj->comp;

	if (comp != NULL)
		g_object_ref (comp);

end:
	g_static_rw_lock_reader_unlock (&fstore->priv->lock);
	return comp;
}

static ECalComponent *
e_cal_backend_file_store_get_component (ECalBackendStore *store,
                                        const gchar *uid,
                                        const gchar *rid)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);

	return get_component (fstore, uid, rid);
}

static gboolean
e_cal_backend_file_store_has_component (ECalBackendStore *store,
                                        const gchar *uid,
                                        const gchar *rid)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);
	gboolean ret_val = FALSE;
	FullCompObject *obj = NULL;

	g_static_rw_lock_reader_lock (&fstore->priv->lock);

	obj = g_hash_table_lookup (fstore->priv->comp_uid_hash, uid);
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
	g_static_rw_lock_reader_unlock (&fstore->priv->lock);
	return ret_val;
}

static gboolean
e_cal_backend_file_store_put_component (ECalBackendStore *store,
                                        ECalComponent *comp)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);
	gboolean ret_val = FALSE;

	ret_val = put_component (fstore, comp);

	if (ret_val) {
		fstore->priv->dirty = TRUE;

		if (!fstore->priv->freeze_changes)
			save_cache (fstore);
	}

	return ret_val;
}

static gboolean
e_cal_backend_file_store_remove_component (ECalBackendStore *store,
                                           const gchar *uid,
                                           const gchar *rid)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);
	gboolean ret_val = FALSE;

	ret_val = remove_component (fstore, uid, rid);

	if (ret_val) {
		fstore->priv->dirty = TRUE;

		if (!fstore->priv->freeze_changes)
			save_cache (fstore);
	}

	return ret_val;
}

static const icaltimezone *
e_cal_backend_file_store_get_timezone (ECalBackendStore *store,
                                       const gchar *tzid)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);
	const icaltimezone *zone = NULL;

	g_static_rw_lock_reader_lock (&fstore->priv->lock);
	zone = g_hash_table_lookup (fstore->priv->timezones, tzid);
	g_static_rw_lock_reader_unlock (&fstore->priv->lock);

	return zone;
}

static gboolean
e_cal_backend_file_store_put_timezone (ECalBackendStore *store,
                                       const icaltimezone *zone)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);
	icaltimezone *copy;

	g_return_val_if_fail (fstore != NULL, FALSE);
	g_return_val_if_fail (zone != NULL, FALSE);

	g_static_rw_lock_writer_lock (&fstore->priv->lock);
	copy = copy_timezone ((icaltimezone *) zone);
	g_hash_table_insert (
		fstore->priv->timezones,
		g_strdup (icaltimezone_get_tzid ((icaltimezone *) zone)),
		copy);
	g_static_rw_lock_writer_unlock (&fstore->priv->lock);

	fstore->priv->dirty = TRUE;

	if (!fstore->priv->freeze_changes)
		save_cache (fstore);

	return TRUE;
}

static gboolean
e_cal_backend_file_store_remove_timezone (ECalBackendStore *store,
                                          const gchar *tzid)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);
	gboolean ret_val = FALSE;

	g_static_rw_lock_writer_lock (&fstore->priv->lock);
	ret_val = g_hash_table_remove (fstore->priv->timezones, tzid);
	g_static_rw_lock_writer_unlock (&fstore->priv->lock);

	if (ret_val) {
		fstore->priv->dirty = TRUE;

		if (!fstore->priv->freeze_changes)
			save_cache (fstore);
	}

	return ret_val;
}

static const gchar *
e_cal_backend_file_store_get_key_value (ECalBackendStore *store,
                                        const gchar *key)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);
	const gchar *value;

	g_static_rw_lock_reader_lock (&fstore->priv->lock);
	value = e_file_cache_get_object (fstore->priv->keys_cache, key);
	g_static_rw_lock_reader_unlock (&fstore->priv->lock);

	return value;
}

static gboolean
e_cal_backend_file_store_put_key_value (ECalBackendStore *store,
                                        const gchar *key,
                                        const gchar *value)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);
	gboolean ret_val = FALSE;

	g_static_rw_lock_writer_lock (&fstore->priv->lock);

	if (!value)
		ret_val = e_file_cache_remove_object (
			fstore->priv->keys_cache, key);
	else {
		if (e_file_cache_get_object (fstore->priv->keys_cache, key))
			ret_val = e_file_cache_replace_object (
				fstore->priv->keys_cache, key, value);
		else
			ret_val = e_file_cache_add_object (
				fstore->priv->keys_cache, key, value);
	}

	g_static_rw_lock_writer_unlock (&fstore->priv->lock);

	return ret_val;
}

static const icaltimezone *
e_cal_backend_file_store_get_default_timezone (ECalBackendStore *store)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);
	const gchar *tzid;
	const icaltimezone *zone = NULL;

	g_static_rw_lock_reader_lock (&fstore->priv->lock);

	tzid = e_file_cache_get_object (
		fstore->priv->keys_cache, "default-zone");
	if (tzid)
		zone = g_hash_table_lookup (fstore->priv->timezones, tzid);

	g_static_rw_lock_reader_unlock (&fstore->priv->lock);

	return zone;
}

static gboolean
e_cal_backend_file_store_set_default_timezone (ECalBackendStore *store,
                                               const icaltimezone *zone)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);
	const gchar *tzid;
	icaltimezone *copy;
	const gchar *key = "default-zone";

	g_static_rw_lock_writer_lock (&fstore->priv->lock);

	tzid = icaltimezone_get_tzid ((icaltimezone *) zone);
	copy = copy_timezone ((icaltimezone *) zone);
	g_hash_table_insert (fstore->priv->timezones, g_strdup (tzid), copy);

	if (e_file_cache_get_object (fstore->priv->keys_cache, key))
		e_file_cache_replace_object (
			fstore->priv->keys_cache, key, tzid);
	else
		e_file_cache_add_object (
			fstore->priv->keys_cache, key, tzid);

	g_static_rw_lock_writer_unlock (&fstore->priv->lock);

	return TRUE;
}

static void
e_cal_backend_file_store_thaw_changes (ECalBackendStore *store)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);

	fstore->priv->freeze_changes = FALSE;

	e_file_cache_thaw_changes (fstore->priv->keys_cache);
	if (fstore->priv->dirty) {
		save_cache (fstore);
	}
}

static void
e_cal_backend_file_store_freeze_changes (ECalBackendStore *store)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);

	fstore->priv->freeze_changes = TRUE;
	e_file_cache_freeze_changes (fstore->priv->keys_cache);
}

static void
add_comp_to_slist (gpointer key,
                   gpointer value,
                   gpointer user_data)
{
	GSList **slist = (GSList **) user_data;
	ECalComponent *comp = (ECalComponent *) value;

	g_object_ref (comp);
	*slist = g_slist_prepend (*slist, comp);
}

static GSList *
e_cal_backend_file_store_get_components_by_uid (ECalBackendStore *store,
                                                const gchar *uid)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);
	FullCompObject *obj = NULL;
	GSList *comps = NULL;

	g_static_rw_lock_reader_lock (&fstore->priv->lock);

	obj = g_hash_table_lookup (fstore->priv->comp_uid_hash, uid);
	if (obj == NULL) {
		goto end;
	}

	if (obj->comp) {
		g_object_ref (obj->comp);
		comps = g_slist_append (comps, obj->comp);
	}

	g_hash_table_foreach (obj->recurrences, (GHFunc) add_comp_to_slist, &comps);
end:
	g_static_rw_lock_reader_unlock (&fstore->priv->lock);
	return comps;
}

static void
add_full_comp_to_slist (gpointer key,
                        gpointer value,
                        gpointer user_data)
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
	GSList *comps = NULL;

	g_static_rw_lock_reader_lock (&fstore->priv->lock);
	g_hash_table_foreach (
		fstore->priv->comp_uid_hash,
		(GHFunc) add_full_comp_to_slist, &comps);
	g_static_rw_lock_reader_unlock (&fstore->priv->lock);

	return comps;
}

static void
add_instance_ids_to_slist (gpointer key,
                           gpointer value,
                           gpointer user_data)
{
	GSList **slist = (GSList **) user_data;
	ECalComponent *comp = (ECalComponent *) value;
	ECalComponentId *id = e_cal_component_get_id (comp);

	*slist = g_slist_prepend (*slist, id);
}

static void
add_comp_ids_to_slist (gpointer key,
                       gpointer value,
                       gpointer user_data)
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
	GSList *comp_ids = NULL;

	g_static_rw_lock_reader_lock (&fstore->priv->lock);
	g_hash_table_foreach (
		fstore->priv->comp_uid_hash,
		(GHFunc) add_comp_ids_to_slist, &comp_ids);
	g_static_rw_lock_reader_unlock (&fstore->priv->lock);

	return comp_ids;
}

static void
add_timezone (ECalBackendFileStore *fstore,
              icalcomponent *vtzcomp)
{
	icalproperty *prop;
	icaltimezone *zone;
	const gchar *tzid;

	prop = icalcomponent_get_first_property (vtzcomp, ICAL_TZID_PROPERTY);
	if (!prop)
		return;

	tzid = icalproperty_get_tzid (prop);
	if (g_hash_table_lookup (fstore->priv->timezones, tzid))
		return;

	zone = icaltimezone_new ();
	if (!icaltimezone_set_component (zone, icalcomponent_new_clone (vtzcomp))) {
		icaltimezone_free (zone, TRUE);
		return;
	}

	g_static_rw_lock_writer_lock (&fstore->priv->lock);
	g_hash_table_insert (fstore->priv->timezones, g_strdup (tzid), zone);
	g_static_rw_lock_writer_unlock (&fstore->priv->lock);
}

static icaltimezone *
resolve_tzid (const gchar *tzid,
              gpointer user_data)
{
	icaltimezone *zone;

	zone = (!strcmp (tzid, "UTC"))
		? icaltimezone_get_utc_timezone ()
		: icaltimezone_get_builtin_timezone_from_tzid (tzid);

	if (!zone)
		zone = (icaltimezone *) e_cal_backend_store_get_timezone (E_CAL_BACKEND_STORE (user_data), tzid);

	return zone;
}

/*static icaltimezone * 
get_zone (icalcomponent *icalcomp)
{
	icalproperty *prop;
	icaltimezone *zone;
	const gchar *tzid;
	prop = icalcomponent_get_first_property (icalcomp, ICAL_TZID_PROPERTY);
	if (!prop)
		return NULL;
	tzid = icalproperty_get_tzid (prop);
	zone = icalcomponent_get_timezone (icalcomp, tzid);
	return zone;
} */

static void
scan_vcalendar (ECalBackendStore *store,
                icalcomponent *top_icalcomp)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);
	icalcompiter iter;
	time_t time_start, time_end;

	for (iter = icalcomponent_begin_component (top_icalcomp, ICAL_ANY_COMPONENT);
	     icalcompiter_deref (&iter) != NULL;
	     icalcompiter_next (&iter)) {
		const icaltimezone *dzone = NULL;
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

		dzone = e_cal_backend_store_get_default_timezone (store);
		e_cal_util_get_component_occur_times (
			comp, &time_start, &time_end,
			resolve_tzid, store, dzone, kind);

		put_component (fstore, comp);
		e_cal_backend_store_interval_tree_add_comp (store, comp, time_start, time_end);

		g_object_unref (comp);
	}
}

static gboolean
e_cal_backend_file_store_load (ECalBackendStore *store)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);
	icalcomponent *icalcomp;

	if (!fstore->priv->cache_file_name || !fstore->priv->key_file_name)
		return FALSE;

	/* Parse keys */
	fstore->priv->keys_cache =
		e_file_cache_new (fstore->priv->key_file_name);

	/* Parse components */
	icalcomp = e_cal_util_parse_ics_file (fstore->priv->cache_file_name);
	if (!icalcomp)
		return FALSE;

	if (icalcomponent_isa (icalcomp) != ICAL_VCALENDAR_COMPONENT) {
		icalcomponent_free (icalcomp);

		return FALSE;
	}
	scan_vcalendar (store, icalcomp);
	icalcomponent_free (icalcomp);

	return TRUE;
}

static gboolean
e_cal_backend_file_store_remove (ECalBackendStore *store)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);

	/* This will remove all the contents in the directory */
	e_file_cache_remove (fstore->priv->keys_cache);

	g_hash_table_destroy (fstore->priv->timezones);
	fstore->priv->timezones = NULL;

	g_hash_table_destroy (fstore->priv->comp_uid_hash);
	fstore->priv->comp_uid_hash = NULL;

	return TRUE;
}

static gboolean
e_cal_backend_file_store_clean (ECalBackendStore *store)
{
	ECalBackendFileStore *fstore = E_CAL_BACKEND_FILE_STORE (store);

	g_static_rw_lock_writer_lock (&fstore->priv->lock);

	e_file_cache_clean (fstore->priv->keys_cache);
	g_hash_table_remove_all (fstore->priv->comp_uid_hash);
	g_hash_table_remove_all (fstore->priv->timezones);

	g_static_rw_lock_writer_unlock (&fstore->priv->lock);

	save_cache (fstore);
	return TRUE;
}

static void
save_instance (gpointer key,
               gpointer value,
               gpointer user_data)
{
	icalcomponent *vcalcomp = user_data;
	icalcomponent *icalcomp;
	ECalComponent *comp = value;

	icalcomp = icalcomponent_new_clone (e_cal_component_get_icalcomponent (comp));
	icalcomponent_add_component (vcalcomp, icalcomp);
}

static void
save_object (gpointer key,
             gpointer value,
             gpointer user_data)
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
save_timezone (gpointer key,
               gpointer tz,
               gpointer vcalcomp)
{
	icalcomponent *tzcomp;

	tzcomp = icalcomponent_new_clone (icaltimezone_get_component (tz));
	icalcomponent_add_component (vcalcomp, tzcomp);
}

static gboolean
timeout_save_cache (gpointer user_data)
{
	ECalBackendFileStore *fstore = user_data;
	icalcomponent *vcalcomp;
	gchar *data = NULL, *tmpfile;
	gsize len, nwrote;
	FILE *f;

	g_static_rw_lock_reader_lock (&fstore->priv->lock);

	fstore->priv->save_timeout_id = 0;

	vcalcomp = e_cal_util_new_top_level ();
	g_hash_table_foreach (
		fstore->priv->timezones, save_timezone, vcalcomp);
	g_hash_table_foreach (
		fstore->priv->comp_uid_hash, save_object, vcalcomp);
	data = icalcomponent_as_ical_string_r (vcalcomp);
	icalcomponent_free (vcalcomp);

	tmpfile = g_strdup_printf ("%s~", fstore->priv->cache_file_name);
	f = g_fopen (tmpfile, "wb");
	if (!f)
		goto error;

	len = strlen (data);
	nwrote = fwrite (data, 1, len, f);
	if (fclose (f) != 0 || nwrote != len)
		goto error;

	if (g_rename (tmpfile, fstore->priv->cache_file_name) != 0)
		g_unlink (tmpfile);

error:
	g_static_rw_lock_reader_unlock (&fstore->priv->lock);
	g_free (tmpfile);
	g_free (data);
	return FALSE;
}

static void
save_cache (ECalBackendFileStore *store)
{
	if (store->priv->save_timeout_id) {
		g_source_remove (store->priv->save_timeout_id);
	}

	store->priv->save_timeout_id = g_timeout_add (
		IDLE_SAVE_TIMEOUT, timeout_save_cache, store);
}

static void
free_timezone (gpointer data)
{
	icaltimezone *zone = data;

	icaltimezone_free (zone, 1);
}

static void
cal_backend_file_store_finalize (GObject *object)
{
	ECalBackendFileStorePrivate *priv;

	priv = E_CAL_BACKEND_FILE_STORE_GET_PRIVATE (object);

	if (priv->save_timeout_id) {
		g_source_remove (priv->save_timeout_id);
		timeout_save_cache (E_CAL_BACKEND_FILE_STORE (object));
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

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_cal_backend_file_store_parent_class)->finalize (object);
}

static void
cal_backend_file_store_constructed (GObject *object)
{
	ECalBackendFileStorePrivate *priv;
	const gchar *path;

	priv = E_CAL_BACKEND_FILE_STORE_GET_PRIVATE (object);

	path = e_cal_backend_store_get_path (E_CAL_BACKEND_STORE (object));
	priv->cache_file_name = g_build_filename (path, CACHE_FILE_NAME, NULL);
	priv->key_file_name = g_build_filename (path, KEY_FILE_NAME, NULL);
}

static void
e_cal_backend_file_store_class_init (ECalBackendFileStoreClass *class)
{
	GObjectClass *object_class;
	ECalBackendStoreClass *store_class;

	g_type_class_add_private (class, sizeof (ECalBackendFileStorePrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = cal_backend_file_store_finalize;
	object_class->constructed = cal_backend_file_store_constructed;

	store_class = E_CAL_BACKEND_STORE_CLASS (class);
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
e_cal_backend_file_store_init (ECalBackendFileStore *store)
{
	store->priv = E_CAL_BACKEND_FILE_STORE_GET_PRIVATE (store);

	store->priv->timezones = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) free_timezone);
	store->priv->comp_uid_hash = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) destroy_full_object);
	store->priv->keys_cache = NULL;
	g_static_rw_lock_init (&store->priv->lock);
	store->priv->cache_file_name = NULL;
	store->priv->key_file_name = NULL;
	store->priv->dirty = FALSE;
	store->priv->freeze_changes = FALSE;
	store->priv->save_timeout_id = 0;
}

/**
 * e_cal_backend_file_store_new:
 *
 * Since: 2.28
 **/
ECalBackendStore *
e_cal_backend_file_store_new (const gchar *path)
{
	return g_object_new (
		E_TYPE_CAL_BACKEND_FILE_STORE,
		"path", path, NULL);
}
