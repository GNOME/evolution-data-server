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

#include <config.h>
#include <libecal/e-cal-util.h>
#include "e-cal-backend-cache.h"

struct _ECalBackendCachePrivate {
	char *uri;
	char *cache_file;
	icalcomponent *top_level;
};

/* Property IDs */
enum {
	PROP_0,
	PROP_URI
};

static GObjectClass *parent_class = NULL;

static icalcomponent *
load_cache_from_uri (char *uri, char **filename)
{
	char *mangled_uri, *c;
	icalcomponent *icalcomp = NULL;

	/* mangle the URI to not contain invalid characters */
	mangled_uri = g_strdup (uri);
	for (c = mangled_uri; c != NULL; c++) {
		switch (*c) {
		case ':' :
		case '/' :
			*c = '_';
		}
	}

	/* open the file */
	*filename = g_build_filename (g_get_home_dir (), ".evolution/calendar/cache",
				      mangled_uri, "cache.ics", NULL);
	icalcomp = e_cal_util_parse_ics_file ((const char *) *filename);
	if (!icalcomp) {
		icalcomp = e_cal_util_new_top_level ();
	} else {
		if (!icalcomponent_isa (icalcomp) == ICAL_VCALENDAR_COMPONENT) {
			icalcomponent_free (icalcomp);
			icalcomp = NULL;
			g_free (*filename);
		}
	}

	/* free memory */
	g_free (mangled_uri);

	return icalcomp;
}

static void
e_cal_backend_cache_set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
	ECalBackendCache *cache;
	ECalBackendCachePrivate *priv;

	cache = E_CAL_BACKEND_CACHE (object);
	priv = cache->priv;

	switch (property_id) {
	case PROP_URI :
		if (!priv->top_level) {
			icalcomponent *icalcomp;
			char *cache_file;

			icalcomp = load_cache_from_uri ((char *) g_value_get_string (value), &cache_file);
			if (!icalcomp)
				break;

			if (priv->uri)
				g_free (priv->uri);
			priv->uri = g_value_dup_string (value);

			if (priv->cache_file)
				g_free (priv->cache_file);
			priv->cache_file = cache_file;

			if (priv->top_level)
				icalcomponent_free (priv->top_level);
			priv->top_level = icalcomp;
		}
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

		if (priv->cache_file) {
			g_free (priv->cache_file);
			priv->cache_file = NULL;
		}

		if (priv->top_level) {
			icalcomponent_free (priv->top_level);
			priv->top_level = NULL;
		}

		g_free (priv);
		cache->priv = NULL;
	}

	parent_class->finalize (object);
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
		type = g_type_register_static (G_TYPE_OBJECT, "ECalBackendCache", &info, 0);
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
