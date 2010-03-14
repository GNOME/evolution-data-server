/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-file-cache.c
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 *
 * Authors: Rodrigo Moya <rodrigo@ximian.com>
 */

#include <config.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "e-file-cache.h"
#include "libedataserver/e-data-server-util.h"
#include "libedataserver/e-xml-hash-utils.h"

struct _EFileCachePrivate {
	gchar *filename;
	EXmlHash *xml_hash;
	gboolean dirty;
	gboolean frozen;
};

/* Property IDs */
enum {
	PROP_0,
	PROP_FILENAME
};

G_DEFINE_TYPE (EFileCache, e_file_cache, G_TYPE_OBJECT)

static void
e_file_cache_set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
	EFileCache *cache;
	EFileCachePrivate *priv;
	gchar *dirname;
	gint result;

	cache = E_FILE_CACHE (object);
	priv = cache->priv;

	/* FIXME: the property is being set twice. Need to investigate
	 * why and fix. Until then, we just return when called the
	 * second time*/
	if (priv->filename)
		return;

	switch (property_id) {
	case PROP_FILENAME :
		/* make sure the directory for the cache exists */
		priv->filename = g_strdup ( g_value_get_string (value));
		dirname = g_path_get_dirname (priv->filename);
		result = g_mkdir_with_parents (dirname, 0700);
		g_free (dirname);
		if (result != 0)
			break;

		if (priv->xml_hash)
			e_xmlhash_destroy (priv->xml_hash);
		priv->xml_hash = e_xmlhash_new (g_value_get_string (value));

		/* if opening the cache file fails, remove it and try again */
		if (!priv->xml_hash) {
			g_unlink (g_value_get_string (value));
			priv->xml_hash = e_xmlhash_new (g_value_get_string (value));
			if (priv->xml_hash) {
				g_message (G_STRLOC ": could not open not re-create cache file %s",
					   g_value_get_string (value));
			}
		}
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

	G_OBJECT_CLASS (e_file_cache_parent_class)->finalize (object);
}

static void
e_file_cache_class_init (EFileCacheClass *klass)
{
	GObjectClass *object_class;

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
	priv->dirty = FALSE;
	priv->frozen = FALSE;
	cache->priv = priv;
}

/**
 * e_file_cache_new
 * @filename: filename where the cache is kept.
 *
 * Creates a new #EFileCache object, which implements a cache of
 * objects, very useful for remote backends.
 *
 * Returns: The newly created object.
 */
EFileCache *
e_file_cache_new (const gchar *filename)
{
	EFileCache *cache;

	cache = g_object_new (E_TYPE_FILE_CACHE, "filename", filename, NULL);

	return cache;
}

/**
 * e_file_cache_remove:
 * @cache: A #EFileCache object.
 *
 * Remove the cache from disk.
 *
 * Returns: TRUE if successful, FALSE otherwise.
 */
gboolean
e_file_cache_remove (EFileCache *cache)
{
	EFileCachePrivate *priv;

	g_return_val_if_fail (E_IS_FILE_CACHE (cache), FALSE);

	priv = cache->priv;

	if (priv->filename) {
		gchar *dirname, *full_path;
		const gchar *fname;
		GDir *dir;
		gboolean success;

		/* remove all files in the directory */
		dirname = g_path_get_dirname (priv->filename);
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
		g_free (priv->filename);
		priv->filename = NULL;

		e_xmlhash_destroy (priv->xml_hash);
		priv->xml_hash = NULL;

		return success;
	}

	return TRUE;
}

static void
add_key_to_slist (const gchar *key, const gchar *value, gpointer user_data)
{
	GSList **keys = user_data;

	*keys = g_slist_append (*keys, (gchar *) key);
}

/**
 * e_file_cache_clean:
 * @cache: A #EFileCache object.
 *
 * Clean up the cache's contents.
 *
 * Returns: TRUE if successful, FALSE otherwise.
 */
gboolean
e_file_cache_clean (EFileCache *cache)
{
	EFileCachePrivate *priv;
	GSList *keys = NULL;
	gboolean iFroze;

	g_return_val_if_fail (E_IS_FILE_CACHE (cache), FALSE);

	priv = cache->priv;
	iFroze = !priv->frozen;

	if (iFroze)
		e_file_cache_freeze_changes (cache);

	e_xmlhash_foreach_key (priv->xml_hash, (EXmlHashFunc) add_key_to_slist, &keys);
	while (keys != NULL) {
		e_file_cache_remove_object (cache, (const gchar *) keys->data);
		keys = g_slist_remove (keys, keys->data);
	}

	if (iFroze)
		e_file_cache_thaw_changes (cache);

	return TRUE;
}

typedef struct {
	const gchar *key;
	gboolean found;
	const gchar *found_value;
} CacheFindData;

static void
find_object_in_hash (gpointer key, gpointer value, gpointer user_data)
{
	CacheFindData *find_data = user_data;

	if (find_data->found)
		return;

	if (!strcmp (find_data->key, (const gchar *) key)) {
		find_data->found = TRUE;
		find_data->found_value = (const gchar *) value;
	}
}

/**
 * e_file_cache_get_object:
 */
const gchar *
e_file_cache_get_object (EFileCache *cache, const gchar *key)
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
add_object_to_slist (const gchar *key, const gchar *value, gpointer user_data)
{
	GSList **list = user_data;

	*list = g_slist_prepend (*list, (gchar *) value);
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

	e_xmlhash_foreach_key (priv->xml_hash, (EXmlHashFunc) add_object_to_slist, &list);

	return list;
}

/**
 * e_file_cache_get_keys:
 */
GSList *
e_file_cache_get_keys (EFileCache *cache)
{
	EFileCachePrivate *priv;
	GSList *list = NULL;

	g_return_val_if_fail (E_IS_FILE_CACHE (cache), NULL);

	priv = cache->priv;

	e_xmlhash_foreach_key (priv->xml_hash, (EXmlHashFunc) add_key_to_slist, &list);

	return list;
}

/**
 * e_file_cache_add_object:
 */
gboolean
e_file_cache_add_object (EFileCache *cache, const gchar *key, const gchar *value)
{
	EFileCachePrivate *priv;

	g_return_val_if_fail (E_IS_FILE_CACHE (cache), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	priv = cache->priv;

	if (e_file_cache_get_object (cache, key))
		return FALSE;

	e_xmlhash_add (priv->xml_hash, key, value);
	if (priv->frozen)
		priv->dirty = TRUE;
	else {
		e_xmlhash_write (priv->xml_hash);
		priv->dirty = FALSE;
	}

	return TRUE;
}

/**
 * e_file_cache_replace_object:
 */
gboolean
e_file_cache_replace_object (EFileCache *cache, const gchar *key, const gchar *new_value)
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
e_file_cache_remove_object (EFileCache *cache, const gchar *key)
{
	EFileCachePrivate *priv;

	g_return_val_if_fail (E_IS_FILE_CACHE (cache), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	priv = cache->priv;

	if (!e_file_cache_get_object (cache, key))
		return FALSE;

	e_xmlhash_remove (priv->xml_hash, key);
	if (priv->frozen)
		priv->dirty = TRUE;
	else {
		e_xmlhash_write (priv->xml_hash);
		priv->dirty = FALSE;
	}

	return TRUE;
}

/**
 * e_file_cache_freeze_changes:
 * @cache: An #EFileCache object.
 *
 * Disables temporarily all writes to disk for the given cache object.
 */
void
e_file_cache_freeze_changes (EFileCache *cache)
{
	EFileCachePrivate *priv;

	g_return_if_fail (E_IS_FILE_CACHE (cache));

	priv = cache->priv;

	priv->frozen = TRUE;
}

/**
 * e_file_cache_thaw_changes:
 * @cache: An #EFileCache object.
 *
 * Enables again writes to disk on every change.
 */
void
e_file_cache_thaw_changes (EFileCache *cache)
{
	EFileCachePrivate *priv;

	g_return_if_fail (E_IS_FILE_CACHE (cache));

	priv = cache->priv;

	priv->frozen = FALSE;
	if (priv->dirty) {
		e_xmlhash_write (priv->xml_hash);
		priv->dirty = FALSE;
	}
}

/**
 * e_file_cache_get_filename:
 * @cache: A %EFileCache object.
 *
 * Gets the name of the file where the cache is being stored.
 *
 * Returns: The name of the cache.
 */
const gchar *
e_file_cache_get_filename (EFileCache *cache)
{
	g_return_val_if_fail (E_IS_FILE_CACHE (cache), NULL);
	return (const gchar *) cache->priv->filename;
}

