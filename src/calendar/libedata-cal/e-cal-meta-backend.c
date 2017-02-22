/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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
 * SECTION: e-cal-meta-backend
 * @include: libedata-cal/libedata-cal.h
 * @short_description: An #ECalBackend descendant for calendar backends
 *
 * The #ECalMetaBackend is an abstract #ECalBackend descendant which
 * aims to implement all evolution-data-server internals for the backend
 * itself and lefts the backend do as minimum work as possible, like
 * loading and saving components, listing available components and so on,
 * thus the backend implementation can focus on things like converting
 * (possibly) remote data into iCalendar objects and back.
 *
 * As the #ECalMetaBackend uses an #ECalCache, the offline support
 * is provided by default.
 *
 * The structure is thread safe.
 **/

#include "evolution-data-server-config.h"

#include <glib.h>
#include <glib/gi18n-lib.h>

#include "e-cal-backend.h"
#include "e-cal-meta-backend.h"

#define LOCAL_PREFIX "file://"

struct _ECalMetaBackendPrivate {
	GMutex property_lock;
	ECalCache *cache;
};

enum {
	PROP_0,
	PROP_CACHE
};

G_DEFINE_ABSTRACT_TYPE (ECalMetaBackend, e_cal_meta_backend, E_TYPE_CAL_BACKEND)

G_DEFINE_BOXED_TYPE (ECalMetaBackendInfo, e_cal_meta_backend_info, e_cal_meta_backend_info_copy, e_cal_meta_backend_info_free)

/**
 * e_cal_cache_search_data_new:
 * @uid: a component UID; cannot be %NULL
 * @rid: (nullable): a component Recurrence-ID; can be %NULL
 * @revision: (nullable): the component revision; can be %NULL
 *
 * Creates a new #ECalMetaBackendInfo prefilled with the given values.
 *
 * Returns: (transfer full): A new #ECalMetaBackendInfo. Free it with
 *    e_cal_meta_backend_info_new() when no longer needed.
 *
 * Since: 3.26
 **/
ECalMetaBackendInfo *
e_cal_meta_backend_info_new (const gchar *uid,
			     const gchar *rid,
			     const gchar *revision)
{
	ECalMetaBackendInfo *info;

	g_return_val_if_fail (uid != NULL, NULL);

	info = g_new0 (ECalMetaBackendInfo, 1);
	info->uid = g_strdup (uid);
	info->rid = g_strdup (rid && *rid ? rid : NULL);
	info->revision = g_strdup (revision);

	return info;
}

/**
 * e_cal_meta_backend_info_copy:
 * @src: (nullable): a source ECalMetaBackendInfo to copy, or %NULL
 *
 * Returns: (transfer full): Copy of the given @src. Free it with
 *    e_cal_meta_backend_info_free() when no longer needed.
 *    If the @src is %NULL, then returns %NULL as well.
 *
 * Since: 3.26
 **/
ECalMetaBackendInfo *
e_cal_meta_backend_info_copy (const ECalMetaBackendInfo *src)
{
	if (src)
		return NULL;

	return e_cal_meta_backend_info_new (src->uid, src->rid, src->revision);
}

/**
 * e_cal_meta_backend_info_free:
 * @ptr: (nullable): an #ECalMetaBackendInfo
 *
 * Frees the @ptr structure, previously allocated with e_cal_meta_backend_info_new()
 * or e_cal_meta_backend_info_copy().
 *
 * Since: 3.26
 **/
void
e_cal_meta_backend_info_free (gpointer ptr)
{
	ECalMetaBackendInfo *info = ptr;

	if (info) {
		g_free (info->uid);
		g_free (info->rid);
		g_free (info->revision);
		g_free (info);
	}
}

static void
e_cal_meta_backend_set_property (GObject *object,
				 guint property_id,
				 const GValue *value,
				 GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CACHE:
			e_cal_meta_backend_set_cache (
				E_CAL_META_BACKEND (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
e_cal_meta_backend_get_property (GObject *object,
				 guint property_id,
				 GValue *value,
				 GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CACHE:
			g_value_take_object (
				value,
				e_cal_meta_backend_ref_cache (
				E_CAL_META_BACKEND (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
e_cal_meta_backend_dispose (GObject *object)
{
	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_meta_backend_parent_class)->dispose (object);
}

static void
e_cal_meta_backend_finalize (GObject *object)
{
	ECalMetaBackend *meta_backend = E_CAL_META_BACKEND (object);

	g_clear_object (&meta_backend->priv->cache);

	g_mutex_clear (&meta_backend->priv->property_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_meta_backend_parent_class)->finalize (object);
}

static void
e_cal_meta_backend_class_init (ECalMetaBackendClass *klass)
{
	GObjectClass *object_class;

	g_type_class_add_private (klass, sizeof (ECalMetaBackendPrivate));

	object_class = G_OBJECT_CLASS (klass);
	object_class->set_property = e_cal_meta_backend_set_property;
	object_class->get_property = e_cal_meta_backend_get_property;
	object_class->dispose = e_cal_meta_backend_dispose;
	object_class->finalize = e_cal_meta_backend_finalize;

	/**
	 * ECalMetaBackend:cache:
	 *
	 * The #ECalCache being used for this meta backend.
	 **/
	g_object_class_install_property (
		object_class,
		PROP_CACHE,
		g_param_spec_object (
			"cache",
			"Cache",
			"Calendar Cache",
			E_TYPE_CAL_CACHE,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS));
}

static void
e_cal_meta_backend_init (ECalMetaBackend *meta_backend)
{
	meta_backend->priv = G_TYPE_INSTANCE_GET_PRIVATE (meta_backend, E_TYPE_CAL_META_BACKEND, ECalMetaBackendPrivate);

	g_mutex_init (&meta_backend->priv->property_lock);
}

/**
 * e_cal_meta_backend_set_cache:
 * @meta_backend: an #ECalMetaBackend
 * @cache: an #ECalCache to use
 *
 * Sets the @cache as the cache to be used by the @meta_backend.
 * This should be done as soon as possible, like at the end
 * of the constructed() method, thus the other places can
 * safely use it.
 *
 * Note the @meta_backend adds its own reference to the @cache.
 *
 * Since: 3.26
 **/
void
e_cal_meta_backend_set_cache (ECalMetaBackend *meta_backend,
			      ECalCache *cache)
{
	g_return_if_fail (E_IS_CAL_META_BACKEND (meta_backend));
	g_return_if_fail (E_IS_CAL_CACHE (cache));

	g_mutex_lock (&meta_backend->priv->property_lock);

	if (meta_backend->priv->cache == cache) {
		g_mutex_unlock (&meta_backend->priv->property_lock);
		return;
	}

	g_clear_object (&meta_backend->priv->cache);
	meta_backend->priv->cache = g_object_ref (cache);

	g_mutex_unlock (&meta_backend->priv->property_lock);

	g_object_notify (G_OBJECT (meta_backend), "cache");
}

/**
 * e_cal_meta_backend_ref_cache:
 * @meta_backend: an #ECalMetaBackend
 *
 * Returns: (transfer full): Referenced #ECalCache, which is used by @meta_backend.
 *    Unref it with g_object_unref() when no longer needed.
 *
 * Since: 3.26
 **/
ECalCache *
e_cal_meta_backend_ref_cache (ECalMetaBackend *meta_backend)
{
	ECalCache *cache;

	g_return_val_if_fail (E_IS_CAL_META_BACKEND (meta_backend), NULL);

	g_mutex_lock (&meta_backend->priv->property_lock);

	if (meta_backend->priv->cache)
		cache = g_object_ref (meta_backend->priv->cache);
	else
		cache = NULL;

	g_mutex_unlock (&meta_backend->priv->property_lock);

	return cache;
}

static gint
sort_master_first_cb (gconstpointer a,
		      gconstpointer b)
{
	icalcomponent *ca, *cb;

	ca = e_cal_component_get_icalcomponent ((ECalComponent *) a);
	cb = e_cal_component_get_icalcomponent ((ECalComponent *) b);

	if (!ca) {
		if (!cb)
			return 0;
		else
			return -1;
	} else if (!cb) {
		return 1;
	}

	return icaltime_compare (icalcomponent_get_recurrenceid (ca), icalcomponent_get_recurrenceid (cb));
}

typedef struct {
	ECalCache *cache;
	gboolean replace_tzid_with_location;
	icalcomponent *vcalendar;
	icalcomponent *icalcomp;
} ForeachTzidData;

static void
add_timezone_cb (icalparameter *param,
                 gpointer data)
{
	icaltimezone *tz;
	const gchar *tzid;
	icalcomponent *vtz_comp;
	ForeachTzidData *f_data = (ForeachTzidData *) data;

	tzid = icalparameter_get_tzid (param);
	if (!tzid)
		return;

	tz = icalcomponent_get_timezone (f_data->vcalendar, tzid);
	if (tz)
		return;

	tz = icalcomponent_get_timezone (f_data->icalcomp, tzid);
	if (!tz)
		tz = icaltimezone_get_builtin_timezone_from_tzid (tzid);
	if (!tz && f_data->cache)
		tz = e_timezone_cache_get_timezone (E_TIMEZONE_CACHE (f_data->cache), tzid);
	if (!tz)
		return;

	if (f_data->replace_tzid_with_location) {
		const gchar *location;

		location = icaltimezone_get_location (tz);
		if (location && *location) {
			icalparameter_set_tzid (param, location);
			tzid = location;

			if (icalcomponent_get_timezone (f_data->vcalendar, tzid))
				return;
		}
	}

	vtz_comp = icaltimezone_get_component (tz);

	if (vtz_comp) {
		icalcomponent *clone = icalcomponent_new_clone (vtz_comp);

		if (f_data->replace_tzid_with_location) {
			icalproperty *prop;

			prop = icalcomponent_get_first_property (clone, ICAL_TZID_PROPERTY);
			if (prop) {
				icalproperty_set_tzid (prop, tzid);
			}
		}

		icalcomponent_add_component (f_data->vcalendar, clone);
	}
}

/**
 * e_cal_meta_backend_merge_instances:
 * @meta_backend: an #ECalMetaBackend
 * @instances: (element-type ECalComponent): component instances to merge
 * @replace_tzid_with_location: whether to replace TZID-s with locations
 *
 * Merges all the instances provided in @instances list into one VCALENDAR
 * object, which would eventually contain also all the used timezones.
 * The @instances list should contain the master object and eventually all
 * the detached instances for one component (they all have the same UID).
 *
 * Any TZID property parameters can be replaced with corresponding timezone
 * location, which will not influence the timezone itself.
 *
 * Returns: (transfer full): an #icalcomponent containing a VCALENDAR
 *    component which consists of all the given instances. Free
 *    the returned pointer with icalcomponent_free() when no longer needed.
 *
 * See: e_cal_meta_backend_save_component_sync()
 *
 * Since: 3.26
 **/
icalcomponent *
e_cal_meta_backend_merge_instances (ECalMetaBackend *meta_backend,
				    const GSList *instances,
				    gboolean replace_tzid_with_location)
{
	ForeachTzidData f_data;
	icalcomponent *vcalendar;
	GSList *link, *sorted;

	g_return_val_if_fail (E_IS_CAL_META_BACKEND (meta_backend), NULL);
	g_return_val_if_fail (instances != NULL, NULL);

	sorted = g_slist_sort (g_slist_copy ((GSList *) instances), sort_master_first_cb);

	vcalendar = e_cal_util_new_top_level ();

	f_data.cache = e_cal_meta_backend_ref_cache (meta_backend);
	f_data.replace_tzid_with_location = replace_tzid_with_location;
	f_data.vcalendar = vcalendar;

	for (link = sorted; link; link = g_slist_next (link)) {
		ECalComponent *comp = link->data;
		icalcomponent *icalcomp;
		ForeachTzidData f_data;

		if (!E_IS_CAL_COMPONENT (comp)) {
			g_warn_if_reached ();
			continue;
		}

		icalcomp = icalcomponent_new_clone (e_cal_component_get_icalcomponent (comp));
		icalcomponent_add_component (vcalendar, icalcomp);

		f_data.icalcomp = icalcomp;

		icalcomponent_foreach_tzid (icalcomp, add_timezone_cb, &f_data);
	}

	g_clear_object (&f_data.cache);
	g_slist_free (sorted);

	return vcalendar;
}

/**
 * e_cal_meta_backend_inline_local_attachments_sync:
 * @meta_backend: an #ECalMetaBackend
 * @component: an icalcomponent to work with
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Changes all URL attachments which point to a local file in @component
 * to inline attachments, aka adds the file content into the @component.
 * It also populates FILENAME parameter on the attachment.
 * This is called automatically before e_cal_meta_backend_save_component_sync().
 *
 * The reverse operation is e_cal_meta_backend_store_inline_attachments_sync().
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_meta_backend_inline_local_attachments_sync (ECalMetaBackend *meta_backend,
						  icalcomponent *component,
						  GCancellable *cancellable,
						  GError **error)
{
	icalproperty *prop;
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_CAL_META_BACKEND (meta_backend), FALSE);
	g_return_val_if_fail (component != NULL, FALSE);

	for (prop = icalcomponent_get_first_property (component, ICAL_ATTACH_PROPERTY);
	     prop && success;
	     prop = icalcomponent_get_next_property (component, ICAL_ATTACH_PROPERTY)) {
		icalattach *attach;

		attach = icalproperty_get_attach (prop);
		if (icalattach_get_is_url (attach)) {
			const gchar *url;

			url = icalattach_get_url (attach);
			if (g_str_has_prefix (url, LOCAL_PREFIX)) {
				GFile *file;
				gchar *basename;
				gchar *content;
				gsize len;

				file = g_file_new_for_uri (url);
				basename = g_file_get_basename (file);
				if (g_file_load_contents (file, cancellable, &content, &len, NULL, error)) {
					icalattach *new_attach;
					icalparameter *param;
					gchar *base64;

					base64 = g_base64_encode ((const guchar *) content, len);
					new_attach = icalattach_new_from_data (base64, NULL, NULL);
					g_free (content);
					g_free (base64);

					while (param = icalproperty_get_first_parameter (prop, ICAL_ANY_PARAMETER), param) {
						icalproperty_remove_parameter_by_ref (prop, param);
					}

					icalproperty_set_attach (prop, new_attach);
					icalattach_unref (new_attach);

					param = icalparameter_new_value (ICAL_VALUE_BINARY);
					icalproperty_add_parameter (prop, param);

					param = icalparameter_new_encoding (ICAL_ENCODING_BASE64);
					icalproperty_add_parameter (prop, param);

					param = icalparameter_new_filename (basename);
					icalproperty_add_parameter (prop, param);
				} else {
					success = FALSE;
				}

				g_object_unref (file);
				g_free (basename);
			}
		}
	}

	return success;
}

/**
 * e_cal_meta_backend_store_inline_attachments_sync:
 * @meta_backend: an #ECalMetaBackend
 * @component: an icalcomponent to work with
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Changes all inline attachments to URL attachments in @component, which
 * will point to a local file instead. The function expects FILENAME parameter
 * to be set on the attachment as the file name of it.
 * This is called automatically after e_cal_meta_backend_load_component_sync().
 *
 * The reverse operation is e_cal_meta_backend_inline_local_attachments_sync().
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_meta_backend_store_inline_attachments_sync (ECalMetaBackend *meta_backend,
						  icalcomponent *component,
						  GCancellable *cancellable,
						  GError **error)
{
	gint fileindex;
	icalproperty *prop;
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_CAL_META_BACKEND (meta_backend), FALSE);
	g_return_val_if_fail (component != NULL, FALSE);

	for (prop = icalcomponent_get_first_property (component, ICAL_ATTACH_PROPERTY), fileindex = 0;
	     prop && success;
	     prop = icalcomponent_get_next_property (component, ICAL_ATTACH_PROPERTY), fileindex++) {
		icalattach *attach;

		attach = icalproperty_get_attach (prop);
		if (!icalattach_get_is_url (attach)) {
			icalparameter *param;
			const gchar *basename;
			gsize len = -1;
			gchar *decoded = NULL;
			gchar *local_filename;

			param = icalproperty_get_first_parameter (prop, ICAL_FILENAME_PARAMETER);
			basename = param ? icalparameter_get_filename (param) : NULL;
			if (!basename || !*basename)
				basename = _("attachment.dat");

			local_filename = e_cal_backend_create_cache_filename (E_CAL_BACKEND (meta_backend), icalcomponent_get_uid (component), basename, fileindex);

			if (local_filename) {
				const gchar *content;

				content = (const gchar *) icalattach_get_data (attach);
				decoded = (gchar *) g_base64_decode (content, &len);

				if (g_file_set_contents (local_filename, decoded, len, error)) {
					icalattach *new_attach;
					gchar *url;

					url = g_filename_to_uri (local_filename, NULL, NULL);
					new_attach = icalattach_new_from_url (url);

					icalproperty_set_attach (prop, new_attach);

					icalattach_unref (new_attach);
					g_free (url);
				} else {
					success = FALSE;
				}
			}

			g_free (local_filename);
		}
	}

	return success;
}

/**
 * e_cal_meta_backend_connect_sync:
 * @meta_backend: an #ECalMetaBackend
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * This is called always before any operations which requires
 * a connection to the remote side. It can fail with
 * an #E_CLIENT_ERROR_REPOSITORY_OFFLINE error to indicate
 * that the remote side cannot be currently reached. Other
 * errors are propagated to the caller/client side.
 * This method is not called when the backend is not online.
 *
 * It is mandatory to implement this virtual method by the descendant.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_meta_backend_connect_sync	(ECalMetaBackend *meta_backend,
				 GCancellable *cancellable,
				 GError **error)
{
	ECalMetaBackendClass *klass;

	g_return_val_if_fail (E_IS_CAL_META_BACKEND (meta_backend), FALSE);

	klass = E_CAL_META_BACKEND_GET_CLASS (meta_backend);
	g_return_val_if_fail (klass != NULL, FALSE);
	g_return_val_if_fail (klass->connect_sync != NULL, FALSE);

	return klass->connect_sync (meta_backend, cancellable, error);
}

/**
 * e_cal_meta_backend_disconnect_sync:
 * @meta_backend: an #ECalMetaBackend
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * This is called when the backend goes into offline mode or
 * when the disconnect is required. The implementation should
 * not report any error when it is called and the @meta_backend
 * is not connected.
 *
 * It is mandatory to implement this virtual method by the descendant.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_meta_backend_disconnect_sync (ECalMetaBackend *meta_backend,
				    GCancellable *cancellable,
				    GError **error)
{
	ECalMetaBackendClass *klass;

	g_return_val_if_fail (E_IS_CAL_META_BACKEND (meta_backend), FALSE);

	klass = E_CAL_META_BACKEND_GET_CLASS (meta_backend);
	g_return_val_if_fail (klass != NULL, FALSE);
	g_return_val_if_fail (klass->disconnect_sync != NULL, FALSE);

	return klass->disconnect_sync (meta_backend, cancellable, error);
}

/**
 * e_cal_meta_backend_get_changes_sync:
 * @meta_backend: an #ECalMetaBackend
 * @out_created_objects: (out) (element-type ECalMetaBackendInfo) (transfer full):
 *    a #GSList of #ECalMetaBackendInfo object infos which had been created since
 *    the last check
 * @out_modified_objects: (out) (element-type ECalMetaBackendInfo) (transfer full):
 *    a #GSList of #ECalMetaBackendInfo object infos which had been modified since
 *    the last check
 * @out_removed_objects: (out) (element-type ECalMetaBackendInfo) (transfer full):
 *    a #GSList of #ECalMetaBackendInfo object infos which had been removed since
 *    the last check
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Gathers the changes since the last check which had been done
 * on the remote side.
 *
 * It is optional to implement this virtual method by the descendant.
 * The default implementation calls e_cal_meta_backend_list_existing_sync()
 * and then compares the list with the current content of the local cache
 * and populates the respective lists appropriately.
 *
 * Each output #GSList should be freed with
 * g_slist_free_full (objects, e_cal_meta_backend_info_free);
 * when no longer needed.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_meta_backend_get_changes_sync (ECalMetaBackend *meta_backend,
				     GSList **out_created_objects,
				     GSList **out_modified_objects,
				     GSList **out_removed_objects,
				     GCancellable *cancellable,
				     GError **error)
{
	ECalMetaBackendClass *klass;

	g_return_val_if_fail (E_IS_CAL_META_BACKEND (meta_backend), FALSE);
	g_return_val_if_fail (out_created_objects != NULL, FALSE);
	g_return_val_if_fail (out_modified_objects != NULL, FALSE);
	g_return_val_if_fail (out_removed_objects != NULL, FALSE);

	klass = E_CAL_META_BACKEND_GET_CLASS (meta_backend);
	g_return_val_if_fail (klass != NULL, FALSE);
	g_return_val_if_fail (klass->get_changes_sync != NULL, FALSE);

	return klass->get_changes_sync (meta_backend, out_created_objects, out_modified_objects, out_removed_objects, cancellable, error);
}

/**
 * e_cal_meta_backend_list_existing_sync:
 * @meta_backend: an #ECalMetaBackend
 * @out_existing_objects: (out) (element-type ECalMetaBackendInfo) (transfer full):
 *    a #GSList of #ECalMetaBackendInfo object infos which are stored on the remote side
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Used to get list of all existing objects on the remote side.
 *
 * It is mandatory to implement this virtual method by the descendant.
 *
 * The @out_existing_objects #GSList should be freed with
 * g_slist_free_full (objects, e_cal_meta_backend_info_free);
 * when no longer needed.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_meta_backend_list_existing_sync (ECalMetaBackend *meta_backend,
				       GSList **out_existing_objects,
				       GCancellable *cancellable,
				       GError **error)
{
	ECalMetaBackendClass *klass;

	g_return_val_if_fail (E_IS_CAL_META_BACKEND (meta_backend), FALSE);
	g_return_val_if_fail (out_existing_objects != NULL, FALSE);

	klass = E_CAL_META_BACKEND_GET_CLASS (meta_backend);
	g_return_val_if_fail (klass != NULL, FALSE);
	g_return_val_if_fail (klass->list_existing_sync != NULL, FALSE);

	return klass->list_existing_sync (meta_backend, out_existing_objects, cancellable, error);
}

/**
 * e_cal_meta_backend_save_component_sync:
 * @meta_backend: an #ECalMetaBackend
 * @overwrite_existing: %TRUE when can overwrite existing components, %FALSE otherwise
 * @conflict_resolution: one of #EConflictResolution, what to do on conflicts
 * @instances: (element-type ECalComponent): instances of the component to save
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Saves one component into the remote side. The @instances contain the master
 * object and all the detached instances of the same component (all have the same UID).
 * When the @overwrite_existing is %TRUE, then the descendant can overwrite an object
 * with the same UID on the remote side (usually used for modify). The @conflict_resolution
 * defines what to do when the remote side had made any changes to the object since
 * the last update.
 *
 * The descendant can use e_cal_meta_backend_merge_instances() to merge
 * the instances into one VCALENDAR component, which will contain also
 * used time zones.
 *
 * The components in @instances have already converted locally stored attachments
 * into inline attachments, thus it's not needed to call
 * e_cal_meta_backend_inline_local_attachments_sync() by the descendant.
 *
 * The descendant can use an #E_CLIENT_ERROR_OUT_OF_SYNC error to indicate that
 * the save failed due to made changes on the remote side, and let the @meta_backend
 * to resolve this conflict based on the @conflict_resolution on its own.
 * The #E_CLIENT_ERROR_OUT_OF_SYNC error should not be used when the descendant
 * is able to resolve the conflicts itself.
 *
 * It is mandatory to implement this virtual method by the descendant.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_meta_backend_save_component_sync (ECalMetaBackend *meta_backend,
					gboolean overwrite_existing,
					EConflictResolution conflict_resolution,
					const GSList *instances,
					GCancellable *cancellable,
					GError **error)
{
	ECalMetaBackendClass *klass;

	g_return_val_if_fail (E_IS_CAL_META_BACKEND (meta_backend), FALSE);
	g_return_val_if_fail (instances != NULL, FALSE);

	klass = E_CAL_META_BACKEND_GET_CLASS (meta_backend);
	g_return_val_if_fail (klass != NULL, FALSE);
	g_return_val_if_fail (klass->save_component_sync != NULL, FALSE);

	return klass->save_component_sync (meta_backend, overwrite_existing, conflict_resolution, instances, cancellable, error);
}

/**
 * e_cal_meta_backend_load_component_sync:
 * @meta_backend: an #ECalMetaBackend
 * @uid: a component UID
 * @rid: (nullable): optional component Recurrence-ID, or %NULL
 * @out_component: (out) (transfer full): a loaded component, as icalcomponent
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Loads one component from the remote side. In case the descendant's storage
 * doesn't allow to store detached instances separately it can ignore the @rid
 * and return whole component. The @out_component can be either
 * a VCALENDAR component, which would contain the master object and all of
 * its detached instances, eventually also used time zones, or the requested
 * component of type VEVENT, VJOURNAL or VTODO.
 *
 * It is mandatory to implement this virtual method by the descendant.
 *
 * The returned @out_component should be freed with icalcomponent_free()
 * when no longer needed.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_meta_backend_load_component_sync (ECalMetaBackend *meta_backend,
					const gchar *uid,
					const gchar *rid,
					icalcomponent **out_component,
					GCancellable *cancellable,
					GError **error)
{
	ECalMetaBackendClass *klass;

	g_return_val_if_fail (E_IS_CAL_META_BACKEND (meta_backend), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (out_component != NULL, FALSE);

	klass = E_CAL_META_BACKEND_GET_CLASS (meta_backend);
	g_return_val_if_fail (klass != NULL, FALSE);
	g_return_val_if_fail (klass->load_component_sync != NULL, FALSE);

	return klass->load_component_sync (meta_backend, uid, rid, out_component, cancellable, error);
}

/**
 * e_cal_meta_backend_get_free_busy_sync:
 * @meta_backend: an #ECalMetaBackend
 * @users: (element-type utf8): a #GSList of user mail addresses
 * @start: time range start
 * @end: time range end
 * @out_freebusy: (out) (element-type utf8): a #GSList of iCalendar strings
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Gets Free/Busy information for all the mail addresses specified
 * in the @users list for the given time range. The descendant can
 * use e_cal_meta_backend_notify_free_busy() to let the client side
 * know about Free/Busy information asynchronously, but it should
 * also report all found Free/Busy data in @out_freebusy #GSList.
 *
 * It is optional to implement this virtual method by the descendant.
 * The default implementation checks whether any of the users
 * equals to #CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS and if so, then
 * it returns the Free/Busy information for this user according to
 * the information in the local cache.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_meta_backend_get_free_busy_sync (ECalMetaBackend *meta_backend,
				       const GSList *users,
				       time_t start,
				       time_t end,
				       GSList **out_freebusy,
				       GCancellable *cancellable,
				       GError **error)
{
	ECalMetaBackendClass *klass;

	g_return_val_if_fail (E_IS_CAL_META_BACKEND (meta_backend), FALSE);
	g_return_val_if_fail (users != NULL, FALSE);
	g_return_val_if_fail (out_freebusy != NULL, FALSE);

	klass = E_CAL_META_BACKEND_GET_CLASS (meta_backend);
	g_return_val_if_fail (klass != NULL, FALSE);
	g_return_val_if_fail (klass->get_free_busy_sync != NULL, FALSE);

	return klass->get_free_busy_sync (meta_backend, users, start, end, out_freebusy, cancellable, error);
}

/**
 * e_cal_meta_backend_notify_free_busy:
 * @meta_backend: an #ECalMetaBackend
 * @freebusy: (element-type utf8): a #GSList of iCalendar strings
 *
 * Notifies the client side about Free/Busy information asynchronously.
 * This is usually used within e_cal_meta_backend_get_free_busy_sync().
 *
 * Since: 3.26
 **/
void
e_cal_meta_backend_notify_free_busy (ECalMetaBackend *meta_backend,
				     const GSList *freebusy)
{
	g_return_if_fail (E_IS_CAL_META_BACKEND (meta_backend));

	if (freebusy) {
		EDataCal *data_cal;

		data_cal = e_cal_backend_ref_data_cal (E_CAL_BACKEND (meta_backend));
		if (data_cal) {
			e_data_cal_report_free_busy_data (data_cal, freebusy);
			g_object_unref (data_cal);
		}
	}
}

/**
 * e_cal_meta_backend_discard_alarm_sync:
 * @meta_backend: an #ECalMetaBackend
 * @uid: a component UID
 * @rid: (nullable): optional component Recurrence-ID, or %NULL
 * @auid: alarm UID to discard
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Discards alarm identified by @auid for component identified
 * by @uid and eventually @rid.
 *
 * It is optional to implement this virtual method by the descendant.
 * The default implementation does nothing.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_meta_backend_discard_alarm_sync (ECalMetaBackend *meta_backend,
				       const gchar *uid,
				       const gchar *rid,
				       const gchar *auid,
				       GCancellable *cancellable,
				       GError **error)
{
	ECalMetaBackendClass *klass;

	g_return_val_if_fail (E_IS_CAL_META_BACKEND (meta_backend), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (auid != NULL, FALSE);

	klass = E_CAL_META_BACKEND_GET_CLASS (meta_backend);
	g_return_val_if_fail (klass != NULL, FALSE);
	g_return_val_if_fail (klass->discard_alarm_sync != NULL, FALSE);

	return klass->discard_alarm_sync (meta_backend, uid, rid, auid, cancellable, error);
}
