/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-file-cache.c
 *
 * Copyright (C) 2003 Novell, Inc.
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
 *
 * Authors: Rodrigo Moya <rodrigo@ximian.com>
 */

#include <config.h>
#include <string.h>
#include "e-file-cache.h"
#include "e-util.h"
#include "e-xml-hash-utils.h"

struct _EFileCachePrivate {
	char *filename;
	EXmlHash *xml_hash;
};

/* Property IDs */
enum {
	PROP_0,
	PROP_FILENAME
};

static GObjectClass *parent_class = NULL;

static void
e_file_cache_set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
	EFileCache *cache;
	EFileCachePrivate *priv;
	char *dirname;
	int result;

	cache = E_FILE_CACHE (object);
	priv = cache->priv;

	switch (property_id) {
	case PROP_FILENAME :
		/* make sure the directory for the cache exists */
		dirname = g_path_get_dirname ((const char *) g_value_get_string (value));
		result = e_util_mkdir_hier (dirname, 0700);
		g_free (dirname);
		if (result != 0)
			break;

		if (priv->xml_hash)
			e_xmlhash_destroy (priv->xml_hash);
		priv->xml_hash = e_xmlhash_new ((const char *) g_value_get_string (value));
		break;
	default :
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
e_file_cache_get_property (GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
	EFileCache *cache;
	EFileCachePrivate *priv;

	cache = E_FILE_CACHE (object);
	priv = cache->priv;

	switch (property_id) {
	case PROP_FILENAME :
		g_value_set_string (value, priv->filename);
		break;
	default :
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
e_file_cache_finalize (GObject *object)
{
	EFileCache *cache;
	EFileCachePrivate *priv;

	cache = E_FILE_CACHE (object);
	priv = cache->priv;

	if (priv) {
		if (priv->filename) {
			g_free (priv->filename);
			priv->filename = NULL;
		}

		if (priv->xml_hash) {
			e_xmlhash_destroy (priv->xml_hash);
			priv->xml_hash = NULL;
		}

		g_free (priv);
		cache->priv = NULL;
	}

	parent_class->finalize (object);
}

static void
e_file_cache_class_init (EFileCacheClass *klass)
{
	GObjectClass *object_class;

	parent_class = g_type_class_peek_parent (klass);

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = e_file_cache_finalize;
	object_class->set_property = e_file_cache_set_property;
	object_class->get_property = e_file_cache_get_property;

	g_object_class_install_property (object_class, PROP_FILENAME,
					 g_param_spec_string ("filename", NULL, NULL, "",
							      G_PARAM_READABLE | G_PARAM_WRITABLE
							      | G_PARAM_CONSTRUCT_ONLY));
}

static void
e_file_cache_init (EFileCache *cache)
{
	EFileCachePrivate *priv;

	priv = g_new0 (EFileCachePrivate, 1);
	cache->priv = priv;
}

/**
 * e_file_cache_get_type:
 * @void:
 *
 * Registers the #EFileCache class if necessary, and returns the type ID
 * associated to it.
 *
 * Return value: The type ID of the #EFileCache class.
 **/
GType
e_file_cache_get_type (void)
{
	static GType type = 0;

	if (!type) {
		static GTypeInfo info = {
                        sizeof (EFileCacheClass),
                        (GBaseInitFunc) NULL,
                        (GBaseFinalizeFunc) NULL,
                        (GClassInitFunc) e_file_cache_class_init,
                        NULL, NULL,
                        sizeof (EFileCache),
                        0,
                        (GInstanceInitFunc) e_file_cache_init,
                };
		type = g_type_register_static (G_TYPE_OBJECT, "EFileCache", &info, 0);
	}

	return type;
}

/**
 * e_file_cache_new
 * @filename: filename where the cache is kept.
 *
 * Creates a new #EFileCache object, which implements a cache of
 * objects, very useful for remote backends.
 *
 * Return value: The newly created object.
 */
EFileCache *
e_file_cache_new (const char *filename)
{
	EFileCache *cache;

	cache = g_object_new (E_TYPE_FILE_CACHE, "filename", filename, NULL);

	return cache;
}

typedef struct {
	const char *key;
	gboolean found;
	const char *found_value;
} CacheFindData;

static void
find_object_in_hash (gpointer key, gpointer value, gpointer user_data)
{
	CacheFindData *find_data = user_data;

	if (find_data->found)
		return;

	if (!strcmp (find_data->key, (const char *) key)) {
		find_data->found = TRUE;
		find_data->found_value = (const char *) value;
	}
}

/**
 * e_file_cache_get_object:
 */
const char *
e_file_cache_get_object (EFileCache *cache, const char *key)
{
	CacheFindData find_data;
	EFileCachePrivate *priv;

	g_return_val_if_fail (E_IS_FILE_CACHE (cache), NULL);
	g_return_val_if_fail (key != NULL, NULL);

	priv = cache->priv;

	find_data.key = key;
	find_data.found = FALSE;
	find_data.found_value = NULL;

	e_xmlhash_foreach_key (priv->xml_hash, (EXmlHashFunc) find_object_in_hash, &find_data);

	return find_data.found_value;
}

static void
add_object_to_list (const char *key, const char *value, gpointer user_data)
{
	GSList **list = user_data;

	*list = g_slist_prepend (*list, value);
}

/**
 * e_file_cache_get_objects:
 */
GSList *
e_file_cache_get_objects (EFileCache *cache)
{
	EFileCachePrivate *priv;
	GSList *list = NULL;

	g_return_val_if_fail (E_IS_FILE_CACHE (cache), NULL);

	priv = cache->priv;

	e_xmlhash_foreach_key (priv->xml_hash, (EXmlHashFunc) add_object_to_list, &list);

	return list;
}

/**
 * e_file_cache_add_object:
 */
gboolean
e_file_cache_add_object (EFileCache *cache, const char *key, const char *value)
{
	EFileCachePrivate *priv;

	g_return_val_if_fail (E_IS_FILE_CACHE (cache), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	priv = cache->priv;

	if (e_file_cache_get_object (cache, key))
		return FALSE;

	e_xmlhash_add (priv->xml_hash, key, value);
	e_xmlhash_write (priv->xml_hash);

	return TRUE;
}

/**
 * e_file_cache_replace_object:
 */
gboolean
e_file_cache_replace_object (EFileCache *cache, const char *key, const char *new_value)
{
	EFileCachePrivate *priv;

	g_return_val_if_fail (E_IS_FILE_CACHE (cache), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	priv = cache->priv;

	if (!e_file_cache_get_object (cache, key))
		return FALSE;

	if (!e_file_cache_remove_object (cache, key))
		return FALSE;

	return e_file_cache_add_object (cache, key, new_value);
}

/**
 * e_file_cache_remove_object:
 */
gboolean
e_file_cache_remove_object (EFileCache *cache, const char *key)
{
	EFileCachePrivate *priv;

	g_return_val_if_fail (E_IS_FILE_CACHE (cache), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	priv = cache->priv;

	if (!e_file_cache_get_object (cache, key))
		return FALSE;

	e_xmlhash_remove (priv->xml_hash, key);
	e_xmlhash_write (priv->xml_hash);

	return TRUE;
}

/**
 * e_file_cache_get_filename:
 * @cache: A %EFileCache object.
 *
 * Gets the name of the file where the cache is being stored.
 *
 * Return value: The name of the cache.
 */
const char *
e_file_cache_get_filename (EFileCache *cache)
{
	g_return_val_if_fail (E_IS_FILE_CACHE (cache), NULL);
	return (const char *) cache->priv->filename;
}

