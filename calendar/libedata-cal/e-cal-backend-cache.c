/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar - generic backend class
 *
 * Copyright (C) 2003 Novell, Inc.
 *
 * Authors: Rodrigo Moya <rodrigo@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <libecal/e-cal-util.h>
#include "e-cal-backend-cache.h"

struct _ECalBackendCachePrivate {
	char *uri;
	GHashTable *timezones;
};

/* Property IDs */
enum {
	PROP_0,
	PROP_URI
};

static GObjectClass *parent_class = NULL;

static char *
get_filename_from_uri (const char *uri)
{
	char *mangled_uri, *filename;
	int i;

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
	filename = g_build_filename (g_get_home_dir (), ".evolution/cache/calendar",
				     mangled_uri, "cache.xml", NULL);

	/* free memory */
	g_free (mangled_uri);

	return filename;
}

static void
e_cal_backend_cache_set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
	ECalBackendCache *cache;
	ECalBackendCachePrivate *priv;
	char *cache_file;

	cache = E_CAL_BACKEND_CACHE (object);
	priv = cache->priv;

	switch (property_id) {
	case PROP_URI :
		cache_file = get_filename_from_uri (g_value_get_string (value));
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
	case PROP_URI :
		g_value_set_string (value, priv->uri);
		break;
	default :
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
free_timezone_hash (gpointer key, gpointer value, gpointer user_data)
{
	g_free (key);
	icaltimezone_free (value, 1);
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

		if (priv->timezones) {
			g_hash_table_foreach (priv->timezones, (GHFunc) free_timezone_hash, NULL);
			g_hash_table_destroy (priv->timezones);
			priv->timezones = NULL;
		}

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
	const char *uri;
	ECalBackendCacheClass *klass;
	GObjectClass *parent_class;

	/* Invoke parent constructor. */
	klass = E_CAL_BACKEND_CACHE_CLASS (g_type_class_peek (E_TYPE_CAL_BACKEND_CACHE));
	parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (klass));
	obj = parent_class->constructor (type,
					 n_construct_properties,
					 construct_properties);
  
	/* extract uid */
	if (!g_ascii_strcasecmp ( g_param_spec_get_name (construct_properties->pspec), "uri")) {
		char *cache_file;

		uri = g_value_get_string (construct_properties->value);
		cache_file = get_filename_from_uri (uri);
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
	g_object_class_install_property (object_class, PROP_URI,
					 g_param_spec_string ("uri", NULL, NULL, "",
							      G_PARAM_READABLE | G_PARAM_WRITABLE
							      | G_PARAM_CONSTRUCT_ONLY));
}

static void
e_cal_backend_cache_init (ECalBackendCache *cache)
{
	ECalBackendCachePrivate *priv;

	priv = g_new0 (ECalBackendCachePrivate, 1);
	priv->timezones = g_hash_table_new (g_str_hash, g_str_equal);

	cache->priv = priv;

}

/**
 * e_cal_backend_cache_get_type:
 * @void:
 *
 * Registers the #ECalBackendCache class if necessary, and returns the type ID
 * associated to it.
 *
 * Return value: The type ID of the #ECalBackendCache class.
 **/
GType
e_cal_backend_cache_get_type (void)
{
	static GType type = 0;

	if (!type) {
		static GTypeInfo info = {
                        sizeof (ECalBackendCacheClass),
                        (GBaseInitFunc) NULL,
                        (GBaseFinalizeFunc) NULL,
                        (GClassInitFunc) e_cal_backend_cache_class_init,
                        NULL, NULL,
                        sizeof (ECalBackendCache),
                        0,
                        (GInstanceInitFunc) e_cal_backend_cache_init,
                };
		type = g_type_register_static (E_TYPE_FILE_CACHE, "ECalBackendCache", &info, 0);
	}

	return type;
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
e_cal_backend_cache_new (const char *uri)
{
	ECalBackendCache *cache;
        
       	cache = g_object_new (E_TYPE_CAL_BACKEND_CACHE, "uri", uri, NULL);

        return cache;
}

static char *
get_key (const char *uid, const char *rid)
{
	GString *real_key;
	char *retval;

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
e_cal_backend_cache_get_component (ECalBackendCache *cache, const char *uid, const char *rid)
{
	char *real_key;
	const char *comp_str;
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
	char *real_key, *uid, *comp_str;
	const char *rid;
	gboolean retval;
	ECalBackendCachePrivate *priv;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CACHE (cache), FALSE);
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), FALSE);

	priv = cache->priv;

	e_cal_component_get_uid (comp, (const char **) &uid);
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
				      const char *uid,
				      const char *rid)
{
	char *real_key;
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
        char *comp_str;
        GSList *l;
	GList *list = NULL;
	icalcomponent *icalcomp;
	ECalComponent *comp = NULL;
        
        /* return null if cache is not a valid Backend Cache.  */
	g_return_val_if_fail (E_IS_CAL_BACKEND_CACHE (cache), NULL);
        l = e_file_cache_get_objects (E_FILE_CACHE (cache));
        if (!l)
                return NULL;
        for ( ; l != NULL; l = g_slist_next (l)) {
                comp_str = l->data;
                if (comp_str) {
                        icalcomp = icalparser_parse_string (comp_str);
                        if (icalcomp) {
				icalcomponent_kind kind;

				kind = icalcomponent_isa (icalcomp);
				if (kind == ICAL_VEVENT_COMPONENT || kind == ICAL_VTODO_COMPONENT) {
					comp = e_cal_component_new ();
					if (e_cal_component_set_icalcomponent (comp, icalcomp))
						list = g_list_append (list, comp);
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
 * e_cal_backend_cache_get_timezone:
 * @cache: An #ECalBackendCache object.
 * @tzid: ID of the timezone to retrieve.
 *
 * Retrieves a timezone component from the cache.
 *
 * Return value: The timezone if found, or NULL otherwise.
 */
const icaltimezone *
e_cal_backend_cache_get_timezone (ECalBackendCache *cache, const char *tzid)
{
	icaltimezone *zone;
	const char *comp_str;
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
	gpointer orig_key, orig_value;
	icaltimezone *new_zone;
	icalcomponent *icalcomp;
	gboolean retval;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CACHE (cache), FALSE);
	g_return_val_if_fail (zone != NULL, FALSE);

	priv = cache->priv;

	/* add the timezone to the cache file */
	icalcomp = icaltimezone_get_component (zone);
	if (!icalcomp)
		return FALSE;

	if (e_file_cache_get_object (E_FILE_CACHE (cache), icaltimezone_get_tzid (zone))) {
		retval = e_file_cache_replace_object (E_FILE_CACHE (cache),
						      icaltimezone_get_tzid (zone),
						      icalcomponent_as_ical_string (icalcomp));
	} else {
		retval = e_file_cache_add_object (E_FILE_CACHE (cache),
						  icaltimezone_get_tzid (zone),
						  icalcomponent_as_ical_string (icalcomp));
	}

	if (!retval)
		return FALSE;

	/* check if the timezone already exists */
	if (g_hash_table_lookup_extended (priv->timezones, icaltimezone_get_tzid (zone),
					  &orig_key, &orig_value)) {
		/* remove the previous timezone */
		g_hash_table_remove (priv->timezones, orig_key);
		g_free (orig_key);
		icaltimezone_free (orig_value, 1);
	}

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

	g_return_val_if_fail (E_IS_CAL_BACKEND_CACHE (cache), FALSE);

	priv = cache->priv;

	/* add the timezone to the cache file */
	icalcomp = icaltimezone_get_component (default_zone);
	if (!icalcomp)
		return FALSE;

	if (e_file_cache_get_object (E_FILE_CACHE (cache), "default_zone")) {
		retval = e_file_cache_replace_object (E_FILE_CACHE (cache), "default_zone",
						      icalcomponent_as_ical_string (icalcomp));
	} else {
		retval = e_file_cache_add_object (E_FILE_CACHE (cache),
						 "default_zone",
						  icalcomponent_as_ical_string (icalcomp));
	}

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
	const char *comp_str;
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
e_cal_backend_cache_remove_timezone (ECalBackendCache *cache, const char *tzid)
{
	gpointer orig_key, orig_value;
	ECalBackendCachePrivate *priv;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CACHE (cache), FALSE);
	g_return_val_if_fail (tzid != NULL, FALSE);

	priv = cache->priv;

	if (g_hash_table_lookup_extended (priv->timezones, tzid, &orig_key, &orig_value)) {
		g_hash_table_remove (priv->timezones, tzid);
		g_free (orig_key);
		icaltimezone_free (orig_value, 1);
	}

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
const char *
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
e_cal_backend_cache_put_server_utc_time (ECalBackendCache *cache, char *utc_str)
{
	char *value;
	gboolean ret_val = FALSE;
	
	g_return_val_if_fail (E_IS_CAL_BACKEND_CACHE (cache), FALSE);

	value = g_strdup (utc_str);

	if (!(ret_val = e_file_cache_add_object (E_FILE_CACHE (cache), "server_utc_time", value)))
		ret_val = e_file_cache_replace_object (E_FILE_CACHE (cache), "server_utc_time", value);

	g_free (value);

	return ret_val;
}

/**
 * e_cal_backend_cache_get_server_utc_time:
 * @cache: An #ECalBackendCache object.
 *
 * Return value: The server's UTC string.
 */
const char *
e_cal_backend_cache_get_server_utc_time (ECalBackendCache *cache)
{

	g_return_val_if_fail (E_IS_CAL_BACKEND_CACHE (cache), NULL);
	
       	return	e_file_cache_get_object (E_FILE_CACHE (cache), "server_utc_time");
}
