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
};

/* Property IDs */
enum {
	PROP_0,
	PROP_URI
};

static GObjectClass *parent_class = NULL;

static char *
get_filename_from_uri (char *uri)
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
	filename = g_build_filename (g_get_home_dir (), ".evolution/calendar/cache",
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
		cache_file = get_filename_from_uri ((char *) g_value_get_string (value));
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
	char *uri;
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
		uri = g_value_get_string (construct_properties->value);
		g_object_set (obj, "filename", get_filename_from_uri (uri), NULL);
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
			e_cal_component_set_icalcomponent (comp, icalcomp);
		}
	}

	/* free memory */
	g_free (real_key);

	return comp;
}

/**
 * e_cal_backend_cache_put_component:
 */
gboolean
e_cal_backend_cache_put_component (ECalBackendCache *cache,
				   ECalComponent *comp)
{
	char *real_key, *uid, *rid, *comp_str;
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

GSList *
e_cal_backend_cache_get_components (ECalBackendCache *cache)
{
        /* return null if cache is not a valid Backend Cache.  */
	g_return_val_if_fail (E_IS_CAL_BACKEND_CACHE (cache), NULL);
        return e_file_cache_get_objects (E_FILE_CACHE (cache));
}


