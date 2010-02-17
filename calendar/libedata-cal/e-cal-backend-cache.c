/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar - generic backend class
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Rodrigo Moya <rodrigo@ximian.com>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <libecal/e-cal-util.h>
#include "e-cal-backend-cache.h"

G_DEFINE_TYPE (ECalBackendCache, e_cal_backend_cache, E_TYPE_FILE_CACHE)

struct _ECalBackendCachePrivate {
	gchar *uri;
	ECalSourceType source_type;
	GHashTable *timezones;
};

/* Property IDs */
enum {
	PROP_0,
	PROP_SOURCE_TYPE,
	PROP_URI
};

static GObjectClass *parent_class = NULL;

static gchar *
get_filename_from_uri (const gchar *uri, ECalSourceType source_type)
{
	gchar *mangled_uri, *filename;
	const gchar *source = NULL;
	gint i;

	switch (source_type) {
		case E_CAL_SOURCE_TYPE_EVENT :
			source = "calendar";
			break;
		case E_CAL_SOURCE_TYPE_TODO :
			source = "tasks";
			break;
		case E_CAL_SOURCE_TYPE_JOURNAL :
			source = "journal";
			break;
		case E_CAL_SOURCE_TYPE_LAST :
		default :
			break;
	}

	/* mangle the URI to not contain invalid characters */
	mangled_uri = g_strdup (uri);
	for (i = 0; i < strlen (mangled_uri); i++) {
		switch (mangled_uri[i]) {
		case ':' :
		case '/' :
			mangled_uri[i] = '_';
		}
	}

	/* generate the file name */
	filename = g_build_filename (g_get_home_dir (), ".evolution/cache/",
				source, mangled_uri, "cache.xml", NULL);

	/* free memory */
	g_free (mangled_uri);

	return filename;
}

static void
e_cal_backend_cache_set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
	ECalBackendCache *cache;
	ECalBackendCachePrivate *priv;
	gchar *cache_file;
	ECalSourceType source_type;

	cache = E_CAL_BACKEND_CACHE (object);
	priv = cache->priv;

	switch (property_id) {
	case PROP_SOURCE_TYPE :
		source_type = g_value_get_enum (value);
		priv->source_type = source_type;
		break;
	case PROP_URI :
		/* Ensure both properties are set and then create the
		 * cache_file property */
		cache_file = get_filename_from_uri (g_value_get_string (value), priv->source_type);
		if (!cache_file)
			break;

		g_object_set (G_OBJECT (cache), "filename", cache_file, NULL);
		g_free (cache_file);

		if (priv->uri)
			g_free (priv->uri);
		priv->uri = g_value_dup_string (value);
		break;
	default :
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
e_cal_backend_cache_get_property (GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
	ECalBackendCache *cache;
	ECalBackendCachePrivate *priv;

	cache = E_CAL_BACKEND_CACHE (object);
	priv = cache->priv;

	switch (property_id) {
	case PROP_SOURCE_TYPE:
		g_value_set_enum (value, priv->source_type);
	case PROP_URI :
		g_value_set_string (value, priv->uri);
		break;
	default :
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
e_cal_backend_cache_finalize (GObject *object)
{
	ECalBackendCache *cache;
	ECalBackendCachePrivate *priv;

	cache = E_CAL_BACKEND_CACHE (object);
	priv = cache->priv;

	if (priv) {
		if (priv->uri) {
			g_free (priv->uri);
			priv->uri = NULL;
		}

		g_hash_table_destroy (priv->timezones);
		priv->timezones = NULL;

		g_free (priv);
		cache->priv = NULL;
	}

	parent_class->finalize (object);
}

static GObject *
e_cal_backend_cache_constructor (GType type,
                                 guint n_construct_properties,
                                 GObjectConstructParam *construct_properties)
{
	GObject *obj;
	const gchar *uri;
	ECalSourceType source_type = E_CAL_SOURCE_TYPE_EVENT;
	ECalBackendCacheClass *klass;
	GObjectClass *parent_class;

	/* Invoke parent constructor. */
	klass = E_CAL_BACKEND_CACHE_CLASS (g_type_class_peek (E_TYPE_CAL_BACKEND_CACHE));
	parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (klass));
	obj = parent_class->constructor (type,
					 n_construct_properties,
					 construct_properties);

	if (!g_ascii_strcasecmp ( g_param_spec_get_name (construct_properties->pspec), "source_type"))
		source_type = g_value_get_enum (construct_properties->value);
	/* extract uid */
	if (!g_ascii_strcasecmp ( g_param_spec_get_name (construct_properties->pspec), "uri")) {
		gchar *cache_file;

		uri = g_value_get_string (construct_properties->value);
		cache_file = get_filename_from_uri (uri, source_type);
		g_object_set (obj, "filename", cache_file, NULL);
		g_free (cache_file);
	}

	return obj;
}

static void
e_cal_backend_cache_class_init (ECalBackendCacheClass *klass)
{
	GObjectClass *object_class;

	parent_class = g_type_class_peek_parent (klass);

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = e_cal_backend_cache_finalize;
	object_class->set_property = e_cal_backend_cache_set_property;
	object_class->get_property = e_cal_backend_cache_get_property;

        object_class->constructor = e_cal_backend_cache_constructor;
	g_object_class_install_property (object_class, PROP_SOURCE_TYPE,
					 g_param_spec_enum ("source_type", NULL, NULL,
					 e_cal_source_type_enum_get_type (),
					 E_CAL_SOURCE_TYPE_EVENT,
					 G_PARAM_READABLE | G_PARAM_WRITABLE
							      | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class, PROP_URI,
					 g_param_spec_string ("uri", NULL, NULL, "",
							      G_PARAM_READABLE | G_PARAM_WRITABLE
							      | G_PARAM_CONSTRUCT_ONLY));
}

static void
timezones_value_destroy (icaltimezone *zone)
{
	icaltimezone_free (zone, 1);
}

static void
e_cal_backend_cache_init (ECalBackendCache *cache)
{
	ECalBackendCachePrivate *priv;

	priv = g_new0 (ECalBackendCachePrivate, 1);
	priv->timezones = g_hash_table_new_full (
		g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) timezones_value_destroy);

	cache->priv = priv;

}

/**
 * e_cal_backend_cache_new
 * @uri: URI of the backend to be cached.
 *
 * Creates a new #ECalBackendCache object, which implements a cache of
 * calendar/tasks objects, very useful for remote backends.
 *
 * Return value: The newly created object.
 */
ECalBackendCache *
e_cal_backend_cache_new (const gchar *uri, ECalSourceType source_type)
{
	ECalBackendCache *cache;

	cache = g_object_new (E_TYPE_CAL_BACKEND_CACHE, "source_type", source_type, "uri", uri,  NULL);

        return cache;
}

static gchar *
get_key (const gchar *uid, const gchar *rid)
{
	GString *real_key;
	gchar *retval;

	real_key = g_string_new (uid);
	if (rid && *rid) {
		real_key = g_string_append (real_key, "@");
		real_key = g_string_append (real_key, rid);
	}

	retval = real_key->str;
	g_string_free (real_key, FALSE);

	return retval;
}

/**
 * e_cal_backend_cache_get_component:
 * @cache: A %ECalBackendCache object.
 * @uid: The UID of the component to retrieve.
 * @rid: Recurrence ID of the specific detached recurrence to retrieve,
 * or NULL if the whole object is to be retrieved.
 *
 * Gets a component from the %ECalBackendCache object.
 *
 * Return value: The %ECalComponent representing the component found,
 * or %NULL if it was not found in the cache.
 */
ECalComponent *
e_cal_backend_cache_get_component (ECalBackendCache *cache, const gchar *uid, const gchar *rid)
{
	gchar *real_key;
	const gchar *comp_str;
	icalcomponent *icalcomp;
	ECalComponent *comp = NULL;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CACHE (cache), NULL);
	g_return_val_if_fail (uid != NULL, NULL);

	real_key = get_key (uid, rid);

	comp_str = e_file_cache_get_object (E_FILE_CACHE (cache), real_key);
	if (comp_str) {
		icalcomp = icalparser_parse_string (comp_str);
		if (icalcomp) {
			comp = e_cal_component_new ();
			if (!e_cal_component_set_icalcomponent (comp, icalcomp)) {
				icalcomponent_free (icalcomp);
				g_object_unref (comp);
				comp = NULL;
			}
		}
	}

	/* free memory */
	g_free (real_key);

	return comp;
}

/**
 * e_cal_backend_cache_put_component:
 * @cache: An #ECalBackendCache object.
 * @comp: Component to put on the cache.
 *
 * Puts the given calendar component in the given cache. This will add
 * the component if it does not exist or replace it if there was a
 * previous version of it.
 *
 * Return value: TRUE if the operation was successful, FALSE otherwise.
 */
gboolean
e_cal_backend_cache_put_component (ECalBackendCache *cache,
				   ECalComponent *comp)
{
	gchar *real_key, *uid, *comp_str;
	gchar *rid;
	gboolean retval;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CACHE (cache), FALSE);
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), FALSE);

	e_cal_component_get_uid (comp, (const gchar **) &uid);
	if (e_cal_component_is_instance (comp)) {
		rid = e_cal_component_get_recurid_as_string (comp);
	} else
		rid = NULL;

	comp_str = e_cal_component_get_as_string (comp);
	real_key = get_key (uid, rid);

	if (e_file_cache_get_object (E_FILE_CACHE (cache), real_key))
		retval = e_file_cache_replace_object (E_FILE_CACHE (cache), real_key, comp_str);
	else
		retval = e_file_cache_add_object (E_FILE_CACHE (cache), real_key, comp_str);

	g_free (real_key);
	g_free (comp_str);
	g_free (rid);

	return retval;
}

/**
 * e_cal_backend_cache_remove_component:
 * @cache: An #ECalBackendCache object.
 * @uid: UID of the component to remove.
 * @rid: Recurrence-ID of the component to remove. This is used when removing
 * detached instances of a recurring appointment.
 *
 * Removes a component from the cache.
 *
 * Return value: TRUE if the component was removed, FALSE otherwise.
 */
gboolean
e_cal_backend_cache_remove_component (ECalBackendCache *cache,
				      const gchar *uid,
				      const gchar *rid)
{
	gchar *real_key;
	gboolean retval;
	ECalBackendCachePrivate *priv;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CACHE (cache), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	priv = cache->priv;

	real_key = get_key (uid, rid);
	if (!e_file_cache_get_object (E_FILE_CACHE (cache), real_key)) {
		g_free (real_key);
		return FALSE;
	}

	retval = e_file_cache_remove_object (E_FILE_CACHE (cache), real_key);
	g_free (real_key);

	return retval;
}

/**
 * e_cal_backend_cache_get_components:
 * @cache: An #ECalBackendCache object.
 *
 * Retrieves a list of all the components stored in the cache.
 *
 * Return value: A list of all the components. Each item in the list is
 * an #ECalComponent, which should be freed when no longer needed.
 */
GList *
e_cal_backend_cache_get_components (ECalBackendCache *cache)
{
        gchar *comp_str;
        GSList *l;
	GList *list = NULL;
	icalcomponent *icalcomp;
	ECalComponent *comp = NULL;

        /* return null if cache is not a valid Backend Cache.  */
	g_return_val_if_fail (E_IS_CAL_BACKEND_CACHE (cache), NULL);
        l = e_file_cache_get_objects (E_FILE_CACHE (cache));
        if (!l)
                return NULL;
        for (; l != NULL; l = g_slist_next (l)) {
                comp_str = l->data;
                if (comp_str) {
                        icalcomp = icalparser_parse_string (comp_str);
                        if (icalcomp) {
				icalcomponent_kind kind;

				kind = icalcomponent_isa (icalcomp);
				if (kind == ICAL_VEVENT_COMPONENT || kind == ICAL_VTODO_COMPONENT || kind == ICAL_VJOURNAL_COMPONENT) {
					comp = e_cal_component_new ();
					if (e_cal_component_set_icalcomponent (comp, icalcomp))
						list = g_list_prepend (list, comp);
					else {
						icalcomponent_free (icalcomp);
						g_object_unref (comp);
					}
				} else
					icalcomponent_free (icalcomp);
                        }
                }

        }

        return list;
}

/**
 * e_cal_backend_cache_get_components_by_uid:
 * @cache: An #ECalBackendCache object.
 * @uid: ID of the component to retrieve.
 *
 * Retrieves a ical components from the cache.
 *
 * Return value: The list of calendar components if found, or NULL otherwise.
 */
GSList *
e_cal_backend_cache_get_components_by_uid (ECalBackendCache *cache, const gchar *uid)
{
        gchar *comp_str;
        GSList *l;
	GSList *list = NULL;
	icalcomponent *icalcomp;
	ECalComponent *comp = NULL;

        /* return null if cache is not a valid Backend Cache.  */
	g_return_val_if_fail (E_IS_CAL_BACKEND_CACHE (cache), NULL);
        l = e_file_cache_get_objects (E_FILE_CACHE (cache));
        if (!l)
                return NULL;
        for (; l != NULL; l = g_slist_next (l)) {
                comp_str = l->data;
                if (comp_str) {
                        icalcomp = icalparser_parse_string (comp_str);
                        if (icalcomp) {
				icalcomponent_kind kind;

				kind = icalcomponent_isa (icalcomp);
				if (kind == ICAL_VEVENT_COMPONENT || kind == ICAL_VTODO_COMPONENT) {
					comp = e_cal_component_new ();
					if ((e_cal_component_set_icalcomponent (comp, icalcomp)) &&
						!strcmp (icalcomponent_get_uid (icalcomp), uid))
							list = g_slist_prepend (list, comp);
					else {
						g_object_unref (comp);
					}
				} else
					icalcomponent_free (icalcomp);
                        }
                }

        }

        return list;
}

/**
 * e_cal_backend_cache_get_timezone:
 * @cache: An #ECalBackendCache object.
 * @tzid: ID of the timezone to retrieve.
 *
 * Retrieves a timezone component from the cache.
 *
 * Return value: The timezone if found, or NULL otherwise.
 */
const icaltimezone *
e_cal_backend_cache_get_timezone (ECalBackendCache *cache, const gchar *tzid)
{
	icaltimezone *zone;
	const gchar *comp_str;
	ECalBackendCachePrivate *priv;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CACHE (cache), NULL);
	g_return_val_if_fail (tzid != NULL, NULL);

	priv = cache->priv;

	/* we first look for the timezone in the timezones hash table */
	zone = g_hash_table_lookup (priv->timezones, tzid);
	if (zone)
		return (const icaltimezone *) zone;

	/* if not found look for the timezone in the cache */
	comp_str = e_file_cache_get_object (E_FILE_CACHE (cache), tzid);
	if (comp_str) {
		icalcomponent *icalcomp;

		icalcomp = icalparser_parse_string (comp_str);
		if (icalcomp) {
			zone = icaltimezone_new ();
			if (icaltimezone_set_component (zone, icalcomp) == 1)
				g_hash_table_insert (priv->timezones, g_strdup (tzid), zone);
			else {
				icalcomponent_free (icalcomp);
				icaltimezone_free (zone, 1);
				zone = NULL;
			}
		}
	}

	return (const icaltimezone *) zone;
}

/**
 * e_cal_backend_cache_put_timezone:
 * @cache: An #ECalBackendCache object.
 * @zone: The timezone to put on the cache.
 *
 * Puts the given timezone in the cache, adding it, if it did not exist, or
 * replacing it, if there was an older version.
 *
 * Return value: TRUE if the timezone was put on the cache, FALSE otherwise.
 */
gboolean
e_cal_backend_cache_put_timezone (ECalBackendCache *cache, const icaltimezone *zone)
{
	ECalBackendCachePrivate *priv;
	icaltimezone *new_zone;
	icalcomponent *icalcomp;
	gboolean retval;
	gchar *obj;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CACHE (cache), FALSE);
	g_return_val_if_fail (zone != NULL, FALSE);

	priv = cache->priv;

	/* add the timezone to the cache file */
	icalcomp = icaltimezone_get_component ((icaltimezone *)zone);
	if (!icalcomp)
		return FALSE;

	obj = icalcomponent_as_ical_string_r (icalcomp);
	if (e_file_cache_get_object (E_FILE_CACHE (cache), icaltimezone_get_tzid ((icaltimezone *)zone))) {
		retval = e_file_cache_replace_object (E_FILE_CACHE (cache),
						      icaltimezone_get_tzid ((icaltimezone *)zone),
						      obj);
	} else {
		retval = e_file_cache_add_object (E_FILE_CACHE (cache),
						  icaltimezone_get_tzid ((icaltimezone *)zone),
						  obj);
	}
	g_free (obj);

	if (!retval)
		return FALSE;

	/* add the timezone to the hash table */
	new_zone = icaltimezone_new ();
	icaltimezone_set_component (new_zone, icalcomponent_new_clone (icalcomp));
	g_hash_table_insert (priv->timezones, g_strdup (icaltimezone_get_tzid (new_zone)), new_zone);

	return TRUE;
}

/**
 * e_cal_backend_cache_put_default_timezone:
 * @cache: An #ECalBackendCache object.
 * @default_zone: The default timezone.
 *
 * Sets the default timezone on the cache.
 *
 * Return value: TRUE if the operation was successful, FALSE otherwise.
 */
gboolean
e_cal_backend_cache_put_default_timezone (ECalBackendCache *cache, icaltimezone *default_zone)
{
	ECalBackendCachePrivate *priv;
	icalcomponent *icalcomp;
	gboolean retval;
	gchar *obj;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CACHE (cache), FALSE);

	priv = cache->priv;

	/* add the timezone to the cache file */
	icalcomp = icaltimezone_get_component (default_zone);
	if (!icalcomp)
		return FALSE;

	obj = icalcomponent_as_ical_string_r (icalcomp);
	if (e_file_cache_get_object (E_FILE_CACHE (cache), "default_zone")) {
		retval = e_file_cache_replace_object (E_FILE_CACHE (cache), "default_zone",
						      obj);
	} else {
		retval = e_file_cache_add_object (E_FILE_CACHE (cache),
						 "default_zone",
						  obj);
	}
	g_free (obj);

	if (!retval)
		return FALSE;

	return TRUE;

}

/**
 * e_cal_backend_cache_get_default_timezone:
 * @cache: An #ECalBackendCache object.
 *
 * Retrieves the default timezone from the cache.
 *
 * Return value: The default timezone, or NULL if no default timezone
 * has been set on the cache.
 */
icaltimezone *
e_cal_backend_cache_get_default_timezone (ECalBackendCache *cache)
{
	icaltimezone *zone;
	const gchar *comp_str;
	ECalBackendCachePrivate *priv;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CACHE (cache), NULL);

	priv = cache->priv;

	/*  look for the timezone in the cache */
	comp_str = e_file_cache_get_object (E_FILE_CACHE (cache), "default_zone");
	if (comp_str) {
		icalcomponent *icalcomp;

		icalcomp = icalparser_parse_string (comp_str);
		if (icalcomp) {
			zone = icaltimezone_new ();
			if (icaltimezone_set_component (zone, icalcomp) == 1) {
				return zone;
			} else {
				icalcomponent_free (icalcomp);
				icaltimezone_free (zone, 1);
			}
		}
	}

	return NULL;
}

/**
 * e_cal_backend_cache_remove_timezone:
 * @cache: An #ECalBackendCache object.
 * @tzid: ID of the timezone to remove.
 *
 * Removes a timezone component from the cache.
 *
 * Return value: TRUE if the timezone was removed, FALSE otherwise.
 */
gboolean
e_cal_backend_cache_remove_timezone (ECalBackendCache *cache, const gchar *tzid)
{
	ECalBackendCachePrivate *priv;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CACHE (cache), FALSE);
	g_return_val_if_fail (tzid != NULL, FALSE);

	priv = cache->priv;

	g_hash_table_remove (priv->timezones, tzid);

	return e_file_cache_remove_object (E_FILE_CACHE (cache), tzid);
}

/**
 * e_cal_backend_cache_get_keys:
 * @cache: An #ECalBackendCache object.
 *
 * Gets the list of unique keys in the cache file.
 *
 * Return value: A list of all the keys. The items in the list are pointers
 * to internal data, so should not be freed, only the list should.
 */
GSList *
e_cal_backend_cache_get_keys (ECalBackendCache *cache)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND_CACHE (cache), NULL);
        return e_file_cache_get_keys (E_FILE_CACHE (cache));
}

/**
 * e_cal_backend_cache_set_marker:
 * @cache: An #ECalBackendCache object.
 *
 * Marks the cache as populated, to discriminate between an empty calendar
 * and an unpopulated one.
 */
void
e_cal_backend_cache_set_marker (ECalBackendCache *cache)
{
	g_return_if_fail (E_IS_CAL_BACKEND_CACHE (cache));
	e_file_cache_add_object (E_FILE_CACHE (cache), "populated", "TRUE");
}

/**
 * e_cal_backend_cache_get_marker:
 * @cache: An #ECalBackendCache object.
 *
 * Gets the marker of the cache. If this field is present, it means the
 * cache has been populated.
 *
 * Return value: The value of the marker or NULL if the cache is still empty.
 */
const gchar *
e_cal_backend_cache_get_marker (ECalBackendCache *cache)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND_CACHE (cache), NULL);
	if (e_file_cache_get_object (E_FILE_CACHE (cache), "populated"))
		return "";
	return NULL;
}

/**
 * e_cal_backend_cache_put_server_utc_time:
 * @cache: An #ECalBackendCache object.
 * @utc_str: UTC string.
 *
 * Return value: TRUE if the operation was successful, FALSE otherwise.
 */
gboolean
e_cal_backend_cache_put_server_utc_time (ECalBackendCache *cache, const gchar *utc_str)
{
	gboolean ret_val = FALSE;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CACHE (cache), FALSE);

	if (!(ret_val = e_file_cache_add_object (E_FILE_CACHE (cache), "server_utc_time", utc_str)))
		ret_val = e_file_cache_replace_object (E_FILE_CACHE (cache), "server_utc_time", utc_str);

	return ret_val;
}

/**
 * e_cal_backend_cache_get_server_utc_time:
 * @cache: An #ECalBackendCache object.
 *
 * Return value: The server's UTC string.
 */
const gchar *
e_cal_backend_cache_get_server_utc_time (ECalBackendCache *cache)
{

	g_return_val_if_fail (E_IS_CAL_BACKEND_CACHE (cache), NULL);

	return	e_file_cache_get_object (E_FILE_CACHE (cache), "server_utc_time");
}

static gchar *
get_keys_key (const gchar *key)
{
	g_return_val_if_fail (key != NULL, NULL);

	return g_strconcat ("keys::", key, NULL);
}

/**
 * e_cal_backend_cache_put_key_value:
 * @cache: An #ECalBackendCache object.
 * @keyp: The Key parameter to identify uniquely.
 * @valuep: The value for the keyp parameter.
 *
 * Return value: TRUE if the operation was successful, FALSE otherwise.
 */
gboolean
e_cal_backend_cache_put_key_value (ECalBackendCache *cache, const gchar *key, const gchar *value)
{
	gchar *real_key;
	gboolean ret_val = FALSE;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CACHE (cache), FALSE);

	real_key = get_keys_key (key);
	if (!value) {
		e_file_cache_remove_object (E_FILE_CACHE (cache), real_key);
		g_free (real_key);
		return TRUE;
	}

	if (!(ret_val = e_file_cache_add_object (E_FILE_CACHE (cache), real_key, value)))
		ret_val = e_file_cache_replace_object (E_FILE_CACHE (cache), real_key, value);

	g_free (real_key);

	return ret_val;
}

/**
 * e_cal_backend_cache_get_key_value:
 * @cache: An #ECalBackendCache object.
 *
 * Return value: The value.
 */
const gchar *
e_cal_backend_cache_get_key_value (ECalBackendCache *cache, const gchar *key)
{
	gchar *real_key;
	const gchar *value;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CACHE (cache), NULL);

	real_key = get_keys_key (key);
	value = e_file_cache_get_object (E_FILE_CACHE (cache), real_key);
	g_free (real_key);

	return value;
}

gboolean
e_cal_backend_cache_remove (const gchar *uri, ECalSourceType source_type)
{
	gchar *filename;

	filename = get_filename_from_uri (uri, source_type);

	if (g_file_test (filename, G_FILE_TEST_EXISTS)) {
		gchar *dirname, *full_path;
		const gchar *fname;
		GDir *dir;
		gboolean success;

		/* remove all files in the directory */
		dirname = g_path_get_dirname (filename);
		dir = g_dir_open (dirname, 0, NULL);
		if (dir) {
			while ((fname = g_dir_read_name (dir))) {
				full_path = g_build_filename (dirname, fname, NULL);
				if (g_unlink (full_path) != 0) {
					g_free (full_path);
					g_free (dirname);
					g_dir_close (dir);

					return FALSE;
				}

				g_free (full_path);
			}

			g_dir_close (dir);
		}

		/* remove the directory itself */
		success = g_rmdir (dirname) == 0;

		/* free all memory */
		g_free (dirname);
		g_free (filename);
		return success;
	}

	g_free (filename);
	return FALSE;
}
