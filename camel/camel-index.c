/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
 *
 * This library is free software you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "camel-index.h"
#include "camel-object.h"

#define w(x)
#define io(x)
#define d(x) /*(printf ("%s (%d): ", __FILE__, __LINE__),(x))*/

#define CAMEL_INDEX_VERSION (0x01)

#define CAMEL_INDEX_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_INDEX, CamelIndexPrivate))

struct _CamelIndexPrivate {
	gpointer dummy;
};

/* ********************************************************************** */
/* CamelIndex */
/* ********************************************************************** */

G_DEFINE_TYPE (CamelIndex, camel_index, G_TYPE_OBJECT)

static void
index_finalize (GObject *object)
{
	CamelIndex *index = CAMEL_INDEX (object);

	g_free (index->path);

	/* Chain up to parent's finalize () method. */
	G_OBJECT_CLASS (camel_index_parent_class)->finalize (object);
}

static void
camel_index_class_init (CamelIndexClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (CamelIndexPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = index_finalize;
}

static void
camel_index_init (CamelIndex *index)
{
	index->priv = CAMEL_INDEX_GET_PRIVATE (index);
	index->version = CAMEL_INDEX_VERSION;
}

void
camel_index_construct (CamelIndex *idx,
                       const gchar *path,
                       gint flags)
{
	g_free (idx->path);
	idx->path = g_strdup_printf ("%s.index", path);
	idx->flags = flags;
}

gint
camel_index_rename (CamelIndex *idx,
                    const gchar *path)
{
	CamelIndexClass *class;

	g_return_val_if_fail (CAMEL_IS_INDEX (idx), -1);

	class = CAMEL_INDEX_GET_CLASS (idx);
	g_return_val_if_fail (class->rename != NULL, -1);

	if ((idx->state & CAMEL_INDEX_DELETED) == 0)
		return class->rename (idx, path);
	else {
		errno = ENOENT;
		return -1;
	}
}

/**
 * camel_index_set_normalize:
 * @index: a #CamelIndex
 * @func: normalization function
 * @data: user data for @func
 *
 * Since: 2.32
 **/
void
camel_index_set_normalize (CamelIndex *index,
                           CamelIndexNorm func,
                           gpointer data)
{
	g_return_if_fail (CAMEL_IS_INDEX (index));

	index->normalize = func;
	index->normalize_data = data;
}

gint
camel_index_sync (CamelIndex *idx)
{
	CamelIndexClass *class;

	g_return_val_if_fail (CAMEL_IS_INDEX (idx), -1);

	class = CAMEL_INDEX_GET_CLASS (idx);
	g_return_val_if_fail (class->sync != NULL, -1);

	if ((idx->state & CAMEL_INDEX_DELETED) == 0)
		return class->sync (idx);
	else {
		errno = ENOENT;
		return -1;
	}
}

gint
camel_index_compress (CamelIndex *idx)
{
	CamelIndexClass *class;

	g_return_val_if_fail (CAMEL_IS_INDEX (idx), -1);

	class = CAMEL_INDEX_GET_CLASS (idx);
	g_return_val_if_fail (class->compress != NULL, -1);

	if ((idx->state & CAMEL_INDEX_DELETED) == 0)
		return class->compress (idx);
	else {
		errno = ENOENT;
		return -1;
	}
}

gint
camel_index_delete (CamelIndex *idx)
{
	CamelIndexClass *class;
	gint ret;

	g_return_val_if_fail (CAMEL_IS_INDEX (idx), -1);

	class = CAMEL_INDEX_GET_CLASS (idx);
	g_return_val_if_fail (class->delete_ != NULL, -1);

	if ((idx->state & CAMEL_INDEX_DELETED) == 0) {
		ret = class->delete_ (idx);
		idx->state |= CAMEL_INDEX_DELETED;
	} else {
		errno = ENOENT;
		ret = -1;
	}

	return ret;
}

gint
camel_index_has_name (CamelIndex *idx,
                      const gchar *name)
{
	CamelIndexClass *class;

	g_return_val_if_fail (CAMEL_IS_INDEX (idx), FALSE);

	class = CAMEL_INDEX_GET_CLASS (idx);
	g_return_val_if_fail (class->has_name != NULL, FALSE);

	if ((idx->state & CAMEL_INDEX_DELETED) == 0)
		return class->has_name (idx, name);
	else
		return FALSE;
}

CamelIndexName *
camel_index_add_name (CamelIndex *idx,
                      const gchar *name)
{
	CamelIndexClass *class;

	g_return_val_if_fail (CAMEL_IS_INDEX (idx), NULL);

	class = CAMEL_INDEX_GET_CLASS (idx);
	g_return_val_if_fail (class->add_name != NULL, NULL);

	if ((idx->state & CAMEL_INDEX_DELETED) == 0)
		return class->add_name (idx, name);
	else
		return NULL;
}

gint
camel_index_write_name (CamelIndex *idx,
                        CamelIndexName *idn)
{
	CamelIndexClass *class;

	g_return_val_if_fail (CAMEL_IS_INDEX (idx), -1);

	class = CAMEL_INDEX_GET_CLASS (idx);
	g_return_val_if_fail (class->write_name != NULL, -1);

	if ((idx->state & CAMEL_INDEX_DELETED) == 0)
		return class->write_name (idx, idn);
	else {
		errno = ENOENT;
		return -1;
	}
}

CamelIndexCursor *
camel_index_find_name (CamelIndex *idx,
                       const gchar *name)
{
	CamelIndexClass *class;

	g_return_val_if_fail (CAMEL_IS_INDEX (idx), NULL);

	class = CAMEL_INDEX_GET_CLASS (idx);
	g_return_val_if_fail (class->find_name != NULL, NULL);

	if ((idx->state & CAMEL_INDEX_DELETED) == 0)
		return class->find_name (idx, name);
	else
		return NULL;
}

void
camel_index_delete_name (CamelIndex *idx,
                         const gchar *name)
{
	CamelIndexClass *class;

	g_return_if_fail (CAMEL_IS_INDEX (idx));

	class = CAMEL_INDEX_GET_CLASS (idx);
	g_return_if_fail (class->delete_name != NULL);

	if ((idx->state & CAMEL_INDEX_DELETED) == 0)
		class->delete_name (idx, name);
}

CamelIndexCursor *
camel_index_find (CamelIndex *idx,
                  const gchar *word)
{
	CamelIndexClass *class;
	CamelIndexCursor *ret;
	gchar *b = (gchar *) word;

	g_return_val_if_fail (CAMEL_IS_INDEX (idx), NULL);

	class = CAMEL_INDEX_GET_CLASS (idx);
	g_return_val_if_fail (class->find != NULL, NULL);

	if ((idx->state & CAMEL_INDEX_DELETED) != 0)
		return NULL;

	if (idx->normalize)
		b = idx->normalize (idx, word, idx->normalize_data);

	ret = class->find (idx, b);

	if (b != word)
		g_free (b);

	return ret;
}

CamelIndexCursor *
camel_index_words (CamelIndex *idx)
{
	CamelIndexClass *class;

	g_return_val_if_fail (CAMEL_IS_INDEX (idx), NULL);

	class = CAMEL_INDEX_GET_CLASS (idx);
	g_return_val_if_fail (class->words != NULL, NULL);

	if ((idx->state & CAMEL_INDEX_DELETED) == 0)
		return class->words (idx);
	else
		return NULL;
}

/* ********************************************************************** */
/* CamelIndexName */
/* ********************************************************************** */

G_DEFINE_TYPE (CamelIndexName, camel_index_name, G_TYPE_OBJECT)

static void
index_name_dispose (GObject *object)
{
	CamelIndexName *index_name = CAMEL_INDEX_NAME (object);

	if (index_name->index != NULL) {
		g_object_unref (index_name->index);
		index_name->index = NULL;
	}

	/* Chain up to parent's dispose () method. */
	G_OBJECT_CLASS (camel_index_name_parent_class)->dispose (object);
}

static void
camel_index_name_class_init (CamelIndexNameClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = index_name_dispose;
}

static void
camel_index_name_init (CamelIndexName *index_name)
{
}

void
camel_index_name_add_word (CamelIndexName *idn,
                           const gchar *word)
{
	CamelIndexNameClass *class;
	gchar *b = (gchar *) word;

	g_return_if_fail (CAMEL_IS_INDEX_NAME (idn));

	class = CAMEL_INDEX_NAME_GET_CLASS (idn);
	g_return_if_fail (class->add_word != NULL);

	if (idn->index->normalize)
		b = idn->index->normalize (idn->index, word, idn->index->normalize_data);

	class->add_word (idn, b);

	if (b != word)
		g_free (b);
}

gsize
camel_index_name_add_buffer (CamelIndexName *idn,
                             const gchar *buffer,
                             gsize len)
{
	CamelIndexNameClass *class;

	g_return_val_if_fail (CAMEL_IS_INDEX_NAME (idn), 0);

	class = CAMEL_INDEX_NAME_GET_CLASS (idn);
	g_return_val_if_fail (class->add_buffer != NULL, 0);

	return class->add_buffer (idn, buffer, len);
}

/* ********************************************************************** */
/* CamelIndexCursor */
/* ********************************************************************** */

G_DEFINE_TYPE (CamelIndexCursor, camel_index_cursor, G_TYPE_OBJECT)

static void
index_cursor_dispose (GObject *object)
{
	CamelIndexCursor *index_cursor = CAMEL_INDEX_CURSOR (object);

	if (index_cursor->index != NULL) {
		g_object_unref (index_cursor->index);
		index_cursor->index = NULL;
	}

	/* Chain up to parent's dispose () method. */
	G_OBJECT_CLASS (camel_index_cursor_parent_class)->dispose (object);
}

static void
camel_index_cursor_class_init (CamelIndexCursorClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = index_cursor_dispose;
}

static void
camel_index_cursor_init (CamelIndexCursor *index_cursor)
{
}

const gchar *
camel_index_cursor_next (CamelIndexCursor *idc)
{
	CamelIndexCursorClass *class;

	g_return_val_if_fail (CAMEL_IS_INDEX_CURSOR (idc), NULL);

	class = CAMEL_INDEX_CURSOR_GET_CLASS (idc);
	g_return_val_if_fail (class->next != NULL, NULL);

	return class->next (idc);
}

